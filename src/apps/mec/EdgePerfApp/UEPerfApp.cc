#include "apps/mec/EdgePerfApp/UEPerfApp.h"
#include "apps/mec/DeviceApp/DeviceAppMessages/DeviceAppPacket_m.h"
#include "apps/mec/DeviceApp/DeviceAppMessages/DeviceAppPacket_Types.h"
#include "nodes/mec/MECPlatform/MEAppPacket_Types.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/chunk/BytesChunk.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"

#include <fstream>

#include <math.h>

namespace simu5g {

using namespace inet;
using namespace std;

Define_Module(UEPerfApp);

UEPerfApp::UEPerfApp()
{
    selfStart_ = nullptr;
    selfStop_ = nullptr;
}

UEPerfApp::~UEPerfApp()
{
    cancelAndDelete(selfStart_);
    cancelAndDelete(selfStop_);
    cancelAndDelete(unBlockingMsg_);
    cancelAndDelete(sendRequest_);
    // clean all the structures
    //ueRequestMap.clear();
}

void UEPerfApp::initialize(int stage)
{
    EV << "UEPerfApp::initialize - stage " << stage << endl;
    cSimpleModule::initialize(stage);
    // avoid multiple initializations
    if (stage!=inet::INITSTAGE_APPLICATION_LAYER)
        return;


    sno_ = 0;

    //retrieve parameters
    requestPacketSize_ = par("requestPacketSize");
    requestPeriod_ = par("period");
    requestTimeout_ = par("timeout");

    localPort_ = par("localPort");
    deviceAppPort_ = par("deviceAppPort");

    char* deviceAppAddressStr = (char*)par("deviceAppAddress").stringValue();
    deviceAppAddress_ = inet::L3AddressResolver().resolve(deviceAppAddressStr);

    controllerAddress_ = L3AddressResolver().resolve(par("controllerAddress")); // ravensController
    controllerPort = par("controllerPort");

    //binding socket
    socket.setOutputGate(gate("socketOut"));
    socket.bind(localPort_);

    int tos = par("tos");
    if (tos != -1)
        socket.setTos(tos);

    mecAppName = par("mecAppName").stringValue();

    //initializing the auto-scheduling messages
    selfStart_ = new cMessage("selfStart");
    selfStop_ = new cMessage("selfStop");
    sendRequest_ = new cMessage("sendRequest");
    unBlockingMsg_ = new cMessage("unBlockingMsg");
    printLostMessages_ = new cMessage("printLostMessages");

    //starting UERequestApp
    simtime_t startTime = par("startTime");
    EV << "UEPerfApp::initialize - starting sendStartMEWarningAlertApp() in " << startTime << " seconds " << endl;
    scheduleAt(simTime() + 9, selfStart_);

    //testing
    EV << "UEPerfApp::initialize - binding to port: local:" << localPort_ << " , dest:" << deviceAppPort_ << endl;

    // register signals for stats
    processingTime_ = registerSignal("processingTime");
    serviceResponseTime_ = registerSignal("serviceResponseTime");
    upLinkTime_ = registerSignal("upLinkTime");
    downLinkTime_ = registerSignal("downLinkTime");
    responseTime_ = registerSignal("responseTime");

    lostPackets_ = 0;
    lostMessages_ = registerSignal("lostMessages");
    mecHostId_ = registerSignal("mecHostId");
}

void UEPerfApp::handleMessage(cMessage *msg)
{
    EV << "UEPerfApp::handleMessage" << endl;
    // Sender Side
    if (msg->isSelfMessage())
    {
        if(!strcmp(msg->getName(), "selfStart"))
            sendStartMECRequestApp();
        else if(!strcmp(msg->getName(), "selfStop"))
            sendStopApp();
        else if(!strcmp(msg->getName(), "sendRequest"))
            sendRequest();
        else if(!strcmp(msg->getName(), "UeTiemoutMessage")){
            // check if the message is in the map, if it is, increment the lost packet counter and/or send data to a vec file, if not, do nothing
            UeTimeoutMessage *timeout_msg = check_and_cast<UeTimeoutMessage*>(msg);
            handleUeTimeoutMessage(timeout_msg);
        }
        else if(!strcmp(msg->getName(), "printLostMessages"))
            printLostMessages();
        else
            throw cRuntimeError("UEPerfApp::handleMessage - \tWARNING: Unrecognized self message");
    }
    // Receiver Side
    else
    {
        inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
        inet::L3Address ipAdd = packet->getTag<L3AddressInd>()->getSrcAddress();

        /*
         * From Device app
         * device app usually runs in the UE (loopback), but it could also run in other places
         */
        if(ipAdd == deviceAppAddress_ || ipAdd == inet::L3Address("127.0.0.1")) // dev app
        {
            auto mePkt = packet->peekAtFront<DeviceAppPacket>();

            if (mePkt == 0)
                throw cRuntimeError("UEPerfApp::handleMessage - \tFATAL! Error when casting to DeviceAppPacket");

            if( !strcmp(mePkt->getType(), ACK_START_MECAPP) )
                handleAckStartMECRequestApp(msg);
            else if(!strcmp(mePkt->getType(), ACK_STOP_MECAPP))
                handleAckStopMECRequestApp(msg);
            else if(!strcmp(mePkt->getType(), MEH_CHANGE))
                handleChangeMecHost(msg);
            else
                throw cRuntimeError("UEPerfApp::handleMessage - \tFATAL! Error, DeviceAppPacket type %s not recognized", mePkt->getType());
        }
        // From MEC application
        else
        {
            auto mePkt = packet->peekAtFront<RequestResponseAppPacket>();
            if (mePkt == 0)
                throw cRuntimeError("UEPerfApp::handleMessage - \tFATAL! Error when casting to RequestAppPacket");

            if(mePkt->getType() == MECAPP_RESPONSE)
                recvResponse(msg);
            else if(mePkt->getType() == UEAPP_ACK_STOP)
                handleStopApp(msg);
            else
                throw cRuntimeError("UEPerfApp::handleMessage - \tFATAL! Error, RequestAppPacket type %d not recognized", mePkt->getType());
        }
    }
}

void UEPerfApp::finish()
{
    //std::cout << "UERequestApp with deviceApp ip" << deviceAppAddress_ <<  " had " << lostPacketNumber << "  lost packets." << std::endl;
}

void UEPerfApp::sendStartMECRequestApp()
{
    EV << "UEPerfApp::sendStartMECRequestApp - Sending " << START_MEAPP <<" type RequestPacket\n";

    inet::Packet* packet = new inet::Packet("RequestAppStart");
    auto start = inet::makeShared<DeviceAppStartPacket>();

    //instantiation requirements and info
    start->setType(START_MECAPP);
    start->setMecAppName(mecAppName.c_str());
    start->setChunkLength(inet::B(2+mecAppName.size()+1));
    start->addTagIfAbsent<inet::CreationTimeTag>()->setCreationTime(simTime());
    packet->insertAtBack(start);

    socket.sendTo(packet, deviceAppAddress_, deviceAppPort_);

    //rescheduling
    scheduleAt(simTime() + 0.5, selfStart_);
    scheduleAt(simTime() + 1, printLostMessages_);
}

void UEPerfApp::sendStopMECRequestApp()
{
    EV << "UEPerfApp::sendStopMECRequestApp - Sending " << STOP_MEAPP <<" type RequestPacket\n";

    inet::Packet* packet = new inet::Packet("DeviceAppStopPacket");
    auto stop = inet::makeShared<DeviceAppStopPacket>();

    //termination requirements and info
    stop->setType(STOP_MECAPP);
    stop->setChunkLength(inet::B(10));
    stop->addTagIfAbsent<inet::CreationTimeTag>()->setCreationTime(simTime());
    packet->insertAtBack(stop);

    socket.sendTo(packet, deviceAppAddress_, deviceAppPort_);

    if(sendRequest_->isScheduled())
    {
        cancelEvent(sendRequest_);
    }

    //rescheduling
    if(selfStop_->isScheduled())
        cancelEvent(selfStop_);
    scheduleAt(simTime() + 1, selfStop_);
}

void UEPerfApp::handleChangeMecHost(cMessage* msg)
{
    EV << "UEPerfApp::handleChangeMecHost - Received " << MEH_CHANGE <<" type RequestPacket\n";

    inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
    auto pkt = packet->peekAtFront<DeviceAppChangeMecHostPacket>();

    setMecAppAddress(L3AddressResolver().resolve(pkt->getNewMehIpAddress()));
    setMecAppPort(pkt->getNewMehPort());
    
    EV << "UEPerfApp::handleChangeMecHost - Received " << pkt->getType() << " type RequestPacket. New mecApp instance is at: "<< mecAppAddress_<< ":" << mecAppPort_ << endl;

    // Later add the check on the new mecAppAddress_ and mecAppPort_ and send an ack with false if something went wrong.
    // For now we assume that the new mecAppAddress_ and mecAppPort_ were correctly changed.

    inet::Packet* ack_packet = new inet::Packet("DeviceAppChangeMecHostAckPacket");

    auto ack = inet::makeShared<DeviceAppChangeMecHostAckPacket>();
    ack->setType(ACK_MEH_CHANGE);
    ack->setResult(true);
    ack->setRequestNumber(pkt->getRequestNumber());
    ack->setChunkLength(inet::B(2));
    ack_packet->insertAtBack(ack);

    socket.sendTo(ack_packet, deviceAppAddress_, deviceAppPort_);
}

void UEPerfApp::handleAckStartMECRequestApp(cMessage* msg)
{
    EV << "UEPerfApp::handleAckStartMECRequestApp - Received Start ACK packet" << endl;
    inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
    auto pkt = packet->peekAtFront<DeviceAppStartAckPacket>();

    if(pkt->getResult() == true)
    {
        mecAppAddress_ = L3AddressResolver().resolve(pkt->getIpAddress());
        mecAppPort_ = pkt->getPort();
        EV << "UEPerfApp::handleAckStartMECRequestApp - Received " << pkt->getType() << " type RequestPacket. mecApp instance is at: "<< mecAppAddress_<< ":" << mecAppPort_ << endl;
        cancelEvent(selfStart_);
        //scheduling sendStopMEWarningAlertApp()
        if(!selfStop_->isScheduled()){
            simtime_t  stopTime = par("stopTime");
            scheduleAt(simTime() + stopTime, selfStop_);
            EV << "UEPerfApp::handleAckStartMECRequestApp - Starting sendStopMECRequestApp() in " << stopTime << " seconds " << endl;
        }
        //send the first reuqest to the MEC app
        sendRequest();
    }
    else
    {
        EV << "UEPerfApp::handleAckStartMECRequestApp - MEC application cannot be instantiated! Reason: " << pkt->getReason() << endl;
        simtime_t startTime = par("startTime");
        EV << "UEPerfApp::initialize - starting sendStartMEWarningAlertApp() in " << startTime << " seconds " << endl;
        if(!selfStart_->isScheduled())
            scheduleAt(simTime() + startTime, selfStart_);
    }
    delete packet;
}

void UEPerfApp::handleAckStopMECRequestApp(cMessage* msg)
{
    EV << "UEPerfApp::handleAckStopMECRequestApp - Received Stop ACK packet" << endl;

    inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
    auto pkt = packet->peekAtFront<DeviceAppStopAckPacket>();

    EV << "UEPerfApp::handleAckStopMECRequestApp - Received " << pkt->getType() << " type RequestPacket with result: "<< pkt->getResult() << endl;
    if(pkt->getResult() == false)
        EV << "Reason: "<< pkt->getReason() << endl;

    cancelEvent(selfStop_);
}


void UEPerfApp::sendRequest()
{
    EV << "UEPerfApp::sendRequest()" << endl;
    inet::Packet* pkt = new inet::Packet("RequestResponseAppPacket");
    auto req = inet::makeShared<RequestResponseAppPacket>();
    req->setType(UEAPP_REQUEST);
    req->setSno(sno_++);
    req->setRequestSentTimestamp(simTime());
    req->setChunkLength(inet::B(requestPacketSize_));
    req->addTagIfAbsent<inet::CreationTimeTag>()->setCreationTime(simTime());
    ueRequestMap[sno_] = req;
    pkt->insertAtBack(req);

    UeTimeoutMessage *msg = new UeTimeoutMessage("UeTiemoutMessage");
    msg->setSno(sno_);

    socket.sendTo(pkt, mecAppAddress_ , mecAppPort_);

    scheduleAt(simTime() + requestPeriod_, sendRequest_);
    scheduleAt(simTime() + requestTimeout_, msg);
    EV<<"UEPerfApp::sendRequest() - Request sent and stored on stanby with sno [" << sno_ << "]" << endl;
}


void UEPerfApp::handleStopApp(cMessage* msg)
{
    EV << "UEPerfApp::handleStopApp" << endl;
    inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
    auto res = packet->peekAtFront<RequestResponseAppPacket>();

    sendStopMECRequestApp();
}

void UEPerfApp::sendStopApp()
{
    inet::Packet* pkt = new inet::Packet("RequestResponseAppPacket");
    auto req = inet::makeShared<RequestResponseAppPacket>();
    req->setType(UEAPP_STOP);
    req->setRequestSentTimestamp(simTime());
    req->setChunkLength(inet::B(requestPacketSize_));
    req->addTagIfAbsent<inet::CreationTimeTag>()->setCreationTime(simTime());
    pkt->insertAtBack(req);

//    start_ = simTime();

    socket.sendTo(pkt, mecAppAddress_ , mecAppPort_);

}

void UEPerfApp::recvResponse(cMessage* msg)
{
    EV << "UEPerfApp::recvResponse" << endl;
    inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
    auto res = packet->peekAtFront<RequestResponseAppPacket>();

    auto it = ueRequestMap.find(res->getSno());
    if(it != ueRequestMap.end())
    {
        avgRTT.push_back(simTime().dbl() - res->getRequestSentTimestamp().dbl());
        ueRequestMap.erase(it);
    }else{
        return;
    }

    simtime_t upLinkDelay = res->getRequestArrivedTimestamp() - res->getRequestSentTimestamp();
    simtime_t downLinkDelay = simTime()- res->getResponseSentTimestamp();
    simtime_t respTime = simTime()- res->getRequestSentTimestamp();

    mehostId_ = res->getMecHostId();

    //std::cout << "MEC HOST ID" << mecHostId << std::endl;

    EV << "UEPerfApp::recvResponse - message with sno [" << res->getSno() << "] " <<
            "upLinkDelay [" << upLinkDelay << "ms]\t" <<
            "downLinkDelay [" << downLinkDelay << "ms]\t" <<
            "processingTime [" << res->getProcessingTime() << "ms]\t" <<
            "serviceResponseTime [" << res->getServiceResponseTime() << "ms]\t" <<
            "responseTime [" << respTime << "ms]" << 
            "mecHostId [" << mehostId_ << "]" << endl;
    //emit stats
    emit(upLinkTime_, upLinkDelay);
    emit(downLinkTime_, downLinkDelay);
    emit(processingTime_, res->getProcessingTime());
    emit(serviceResponseTime_, res->getServiceResponseTime());
    emit(responseTime_, respTime);
    emit(mecHostId_, mehostId_);

    delete packet;
}

void UEPerfApp::handleUeTimeoutMessage(UeTimeoutMessage* msg)
{
    //check if the sno inside the msg is still on the map
    auto it = ueRequestMap.find(msg->getSno());
    if(it != ueRequestMap.end())
    {
        //increment the lost packet counter 
        lostPackets_++;
        //remove the packet from the map
        ueRequestMap.erase(it);
    }

    delete msg;
}

void UEPerfApp::printLostMessages()
{
    EV << "UEPerfApp::printLostMessages - Lost packets: " << lostPackets_ << endl;
    emit(lostMessages_, lostPackets_);
    sendNetworkUpdates();
    lostPackets_ = 0;
    int frequency = 5*requestTimeout_;
    scheduleAt(simTime() + frequency, printLostMessages_);
}

void UEPerfApp::sendNetworkUpdates()
{
    // average of the avgRTT list
    double avgRTT_ = 0;
    for(auto it = avgRTT.begin(); it != avgRTT.end(); it++)
        avgRTT_ += *it;
    avgRTT_ /= avgRTT.size();
    avgRTT.clear();

    inet::Packet* packet = new inet::Packet("UsersNetworkInfoPacket");
    auto network_info = inet::makeShared<UsersNetworkInfoPacket>();
    // add the string "mecHost" and the mehosId in one string
    std::string mehId = "mecHost" + std::to_string(mehostId_);
    network_info->setChunkLength(inet::B(20));
    network_info->setMecHostId(mehId.c_str());
    network_info->setLostPackets(lostPackets_);
    network_info->setAvgRTT(avgRTT_);
    packet->insertAtBack(network_info);
    socket.sendTo(packet,controllerAddress_,controllerPort);

    EV << "UEPerfApp::sendNetworkUpdates - Sent network info to controller: " << lostPackets_ << " lost packets, " << avgRTT_ << " avg RTT" << endl;
}

void UEPerfApp::setMecAppAddress(L3Address mecAppAddress)
{
    mecAppAddress_ = mecAppAddress;
}

void UEPerfApp::setMecAppPort(int mecAppPort)
{
    mecAppPort_ = mecAppPort;
}

}
