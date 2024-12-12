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

    // run through ueRequestMsgs and ueTimeoutMsgs and delete or cancel them
    for(auto & ueRequestMsg : ueRequestMsgs)
    {
        // if they are schedule messages, cancel them, otherwise delete them
        if(ueRequestMsg->requestMsg->isScheduled())
            cancelAndDelete(ueRequestMsg->requestMsg);
        else
            delete ueRequestMsg->requestMsg;
        delete ueRequestMsg;
    }

    for(auto & ueTimeoutMsg : ueTimeoutMsgs)
    {
        // if they are schedule messages, cancel them, otherwise delete them
        if(ueTimeoutMsg->isScheduled())
            cancelAndDelete(ueTimeoutMsg);
        else
            delete ueTimeoutMsg;
    }
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

    //starting UERequestApp
    simtime_t startTime = par("startTime");
    EV << "UEPerfApp::initialize - starting sendStartMEWarningAlertApp() in " << startTime << " seconds " << endl;
    scheduleAt(simTime() + 10, selfStart_);

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

    ip_0_ = registerSignal("ip_0");
    ip_1_ = registerSignal("ip_1");
    ip_2_ = registerSignal("ip_2");
    ip_3_ = registerSignal("ip_3");

    ueId_ = deviceAppAddress_.str(); 

    std::cout << ueId_ << std::endl;

    // dive the ueId_ into 4 parts divided by the dots and emit the signal for ip_0, ip_1, ip_2, ip_3
    std::string ip = deviceAppAddress_.str();
    ip = ip + ".";
    std::string delimiter = ".";
    size_t pos = 0;
    std::string token;
    int i = 0;
    while ((pos = ip.find(delimiter)) != std::string::npos) {
        token = ip.substr(0, pos);
        if(i == 0)
            emit(ip_0_, std::stoi(token));
        else if(i == 1)
            emit(ip_1_, std::stoi(token));
        else if(i == 2)
            emit(ip_2_, std::stoi(token));
        else if(i == 3)
            emit(ip_3_, std::stoi(token));
        ip.erase(0, pos + delimiter.length());
        i++;
    }

}

void UEPerfApp::handleMessage(cMessage *msg)
{
    EV << "UEPerfApp::handleMessage" << endl;
    // Sender Side
    if (msg->isSelfMessage())
    {
        if(!strcmp(msg->getName(), "selfStart"))
        {
            EV << "UEPerfApp::handleMessage - \tStarting the UEPerfApp" << endl;
            sendStartMECRequestApp();
        }
        else if(!strcmp(msg->getName(), "selfStop"))
        {
            EV << "UEPerfApp::handleMessage - \tStopping the UEPerfApp" << endl;
            sendStopApp();
        }
        else if(!strcmp(msg->getName(), "sendRequest"))
        {
            EV << "UEPerfApp::handleMessage - \tSending a new request with IP " << deviceAppAddress_ << endl;
            sendRequest();
        } 
        else if(!strcmp(msg->getName(), "UeTimeoutMessage"))
        {
            EV << "UEPerfApp::handleMessage - \tHandling a timeout message" << endl;
            // check if the message is in the map, if it is, increment the lost packet counter and/or send data to a vec file, if not, do nothing
            UeTimeoutMessage *timeout_msg = check_and_cast<UeTimeoutMessage*>(msg);
            handleUeTimeoutMessage(timeout_msg);
        }
        else if(!strcmp(msg->getName(), "AckDelay"))
        {
            inet::Packet *packet_to_send = static_cast<inet::Packet*>(msg->getContextPointer());
            socket.sendTo(packet_to_send, deviceAppAddress_, deviceAppPort_);
        }
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
    std::cout << simTime() << " - UEPerfApp with deviceApp ip " << deviceAppAddress_ <<  " finished!" << std::endl;
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

    //rescheduling
    if(selfStop_->isScheduled())
        cancelEvent(selfStop_);
    scheduleAt(simTime() + 1, selfStop_);
}

void UEPerfApp::handleChangeMecHost(cMessage* msg)
{
    inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
    auto pkt = packet->peekAtFront<DeviceAppChangeMecHostPacket>();

    EV << simTime() <<" - UEPerfApp::handleChangeMecHost - Received " << MEH_CHANGE <<" type RequestPacket\n";
    //std::cout << simTime() << " - UEPerfApp::handleChangeMecHost from UE with ip " << deviceAppAddress_ << " - Received " << MEH_CHANGE <<" type RequestPacket to change to ip "<< pkt->getNewMehIpAddress() << " with port " << pkt->getNewMehPort() << "\n";

    setMecAppAddress(L3AddressResolver().resolve(pkt->getNewMehIpAddress()));
    setMecAppPort(pkt->getNewMehPort());

    //sendRequest();
    
    EV << "UEPerfApp::handleChangeMecHost - Received " << pkt->getType() << " type RequestPacket. New mecApp instance is at: "<< mecAppAddress_<< ":" << mecAppPort_ << endl;
    //std::cout << simTime() << "UEPerfApp::handleChangeMecHost - Received " << pkt->getType() << " type RequestPacket. New mecApp instance is at: "<< mecAppAddress_<< ":" << mecAppPort_ << endl;
    // Later add the check on the new mecAppAddress_ and mecAppPort_ and send an ack with false if something went wrong.
    // For now we assume that the new mecAppAddress_ and mecAppPort_ were correctly changed.

    inet::Packet* ack_packet = new inet::Packet("DeviceAppChangeMecHostAckPacket");

    auto ack = inet::makeShared<DeviceAppChangeMecHostAckPacket>();
    ack->setType(ACK_MEH_CHANGE);
    ack->setResult(true);
    ack->setRequestNumber(pkt->getRequestNumber());
    ack->setChunkLength(inet::B(2));
    ack_packet->insertAtBack(ack);

    //socket.sendTo(ack_packet, deviceAppAddress_, deviceAppPort_);

    //delete packet;

    // schedule an ack message to be sent to the device app in 3 seconds
    //AckDelay *ackDelay = new AckDelay("AckDelay");
    //ackDelay->setSno(pkt->getRequestNumber());


    // convert ackDelay to cMessage
    cMessage *ack_delay = new cMessage("AckDelay");
    ack_delay->setContextPointer(ack_packet);
    scheduleAt(simTime() + 3, ack_delay);

    delete packet;
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
        //send the first request to the MEC app
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
    EV << simTime() << " - UEPerfApp::sendRequest() to " << mecAppAddress_ << " with port " << mecAppPort_ << endl;
    inet::Packet* pkt = new inet::Packet("RequestResponseAppPacket");
    auto req = inet::makeShared<RequestResponseAppPacket>();
    req->setType(UEAPP_REQUEST);
    req->setSno(sno_);
    req->setRequestSentTimestamp(simTime());
    req->setChunkLength(inet::B(requestPacketSize_));
    req->addTagIfAbsent<inet::CreationTimeTag>()->setCreationTime(simTime());
    ueRequestMap[sno_] = req;
    pkt->insertAtBack(req);

    UeTimeoutMessage *sendRequestTimeout = new UeTimeoutMessage("UeTimeoutMessage");
    sendRequestTimeout->setSno(sno_);
    cMessage* request = new cMessage("sendRequest");

    socket.sendTo(pkt, mecAppAddress_ , mecAppPort_);

    EV<<"UEPerfApp::sendRequest() - Request sent and stored on stanby with sno [" << sno_ << "]" << endl;
    //std::cout << "UEPerfApp::sendRequest() - Request sent and stored on stanby with sno [" << sno_ << "]" << std::endl;

    scheduleAt(simTime() + requestPeriod_, request); // next request
    scheduleAt(simTime() + requestTimeout_, sendRequestTimeout); // timeout

    // add to the set requestMsgQueue_
    requestMsg* reqMsg = new requestMsg();
    reqMsg->requestMsg = request;
    reqMsg->sno = sno_;
    ueRequestMsgs.push_back(reqMsg);
    ueTimeoutMsgs.push_back(sendRequestTimeout);
    sno_++;
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
    EV << "Car with IP " << deviceAppAddress_ << " UEPerfApp::recvResponse" << endl;
    inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
    auto res = packet->peekAtFront<RequestResponseAppPacket>();

    auto it = ueRequestMap.find(res->getSno());
    if(it != ueRequestMap.end())
    {
        it = ueRequestMap.erase(it);
    }
    else
    {
        EV << "UEPerfApp::recvResponse - Received a response with sno [" << res->getSno() << "] that is not in the map" << endl;
        //std::cout << "entramos aqui" << std::endl;
        delete packet;
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
        //remove the packet from the map
        ueRequestMap.erase(it);
    }

    
    auto it_timeout = std::find(ueTimeoutMsgs.begin(), ueTimeoutMsgs.end(), msg);
    if(it_timeout != ueTimeoutMsgs.end())
    {
        ueTimeoutMsgs.erase(it_timeout); // Remove the pointer from the vector
    } 
    
    delete msg;
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
