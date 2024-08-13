#include "apps/mec/EdgePerfApp/MECPerfApp.h"

#include "nodes/mec/utils/httpUtils/httpUtils.h"
#include "nodes/mec/utils/httpUtils/json.hpp"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpResponseMessage/HttpResponseMessage.h"

#include "apps/mec/EdgePerfApp/packets/RequestResponsePacket_m.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/Packet_m.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"

#include <fstream>
#include "MECPerfApp.h"

namespace simu5g {

Define_Module(MECPerfApp);


using namespace inet;
using namespace omnetpp;

MECPerfApp::MECPerfApp(): MecAppBase()
{
    mp1Socket_ = nullptr;
    serviceSocket_ = nullptr;
    currentRequestfMsg_ = nullptr;
    processingTimer_ = nullptr;
}

MECPerfApp::~MECPerfApp()
{
    delete currentRequestfMsg_;
    cancelAndDelete(processingTimer_);
}

void MECPerfApp::initialize(int stage)
{
    MecAppBase::initialize(stage);

    retryAttempt = 0;

    // avoid multiple initializations
    if (stage != inet::INITSTAGE_APPLICATION_LAYER)
        return;

    EV << "MECPerfApp::initialize - MEC application " << getClassName() << " with mecAppId[" << mecAppId << "] has started!" << endl;

    localUePort_ = par("localUePort");
    ueAppSocket_.setOutputGate(gate("socketOut"));
    ueAppSocket_.bind(localUePort_);
    processing_ = false;

    packetSize_ = par("responsePacketSize");

    processingTimer_  = new cMessage("computeMsg");
    retryMessage_ = new cMessage("retryMsg");

    minInstructions_ = par("minInstructions");
    maxInstructions_ = par("maxInstructions");

    // connect with the service registry
    EV << "MECPerfApp::initialize - Initialize connection with the Service Registry via Mp1" << endl;
    mp1Socket_ = addNewSocket();

    std::string mecHostName = mecHost->getName();
     //subtract mecHost from the name
    mecHostName = mecHostName.substr(7, mecHostName.length());

    mecHostId = stoi(mecHostName);

    connect(mp1Socket_, mp1Address, mp1Port);
}

void MECPerfApp::handleProcessedMessage(cMessage *msg)
{
    if (!msg->isSelfMessage()) {
        if (ueAppSocket_.belongsToSocket(msg)) {
            EV << "MECPerfApp::handleProcessedMessage: received message from UE" << endl;
            inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
            auto req = packet->peekAtFront<RequestResponseAppPacket>();
            if(req->getType() == UEAPP_REQUEST)
                handleRequest(msg);
            else if(req->getType() == UEAPP_STOP)
                handleStopRequest(msg);

            else
                throw cRuntimeError("MECPerfApp::handleProcessedMessage - Type not recognized!");
            return;
        }
    }
    MecAppBase::handleProcessedMessage(msg);
}

void MECPerfApp::finish()
{
    MecAppBase::finish();
    EV << "MECPerfApp::finish()" << endl;
    if (gate("socketOut")->isConnected()) {


    }
}

double MECPerfApp::scheduleNextMsg(cMessage *msg)
{
    return MecAppBase::scheduleNextMsg(msg);
}

void MECPerfApp::handleSelfMessage(cMessage *msg)
{
    if(!strcmp(msg->getName(), "computeMsg"))
    {
        sendResponse();
    }else if(!strcmp(msg->getName(), "retryMsg"))
    {
        handleRequest(currentRequestfMsg_);
    }
}

void MECPerfApp::handleRequest(cMessage* msg)
{
    //currentProcessedMsg_ = nullptr;
    EV << "MECPerfApp::handleRequest" << endl;
    // this method pretends to perform some computation after having
    //.request some info to the RNI
    //if(currentRequestfMsg_  != nullptr)
        
        // throw cRuntimeError("MECResponseApp::handleRequest - currentRequestfMsg_ not null!");

    msgArrived_ = simTime();
    currentRequestfMsg_ = msg;
    sendGetRequest();
    getRequestSent_ = simTime();
}


void MECPerfApp::handleStopRequest(cMessage* msg)
{
    EV << "MECPerfApp::handleStopRequest" << endl;
    serviceSocket_->close();
}
void MECPerfApp::sendResponse()
{
    inet::Packet* packet = check_and_cast<inet::Packet*>(currentRequestfMsg_);
    ueAppAddress = packet->getTag<L3AddressInd>()->getSrcAddress();
    ueAppPort  = packet->getTag<L4PortInd>()->getSrcPort();

    // do it sooner
    currentRequestfMsg_ = nullptr;

    auto req = packet->removeAtFront<RequestResponseAppPacket>();
    req->setType(MECAPP_RESPONSE);
    req->setMecHostId(mecHostId);
    req->setRequestArrivedTimestamp(msgArrived_);
    req->setServiceResponseTime(getRequestArrived_ - getRequestSent_);
    req->setResponseSentTimestamp(simTime());
    req->setProcessingTime(processingTime_);
    req->setChunkLength(B(packetSize_));
    inet::Packet* pkt = new inet::Packet("ResponseAppPacket");
    pkt->insertAtBack(req);

    ueAppSocket_.sendTo(pkt, ueAppAddress, ueAppPort);

    //clean current request
    delete packet;
    // currentRequestfMsg_ = nullptr;
    msgArrived_ = 0;
    processingTime_ = 0;
    getRequestArrived_ = 0;
    getRequestSent_ = 0;
}

void MECPerfApp::handleHttpMessage(int connId)
{
    if (mp1Socket_ != nullptr && connId == mp1Socket_->getSocketId()) {
        handleMp1Message(connId);
    }
    else                // if (connId == serviceSocket_->getSocketId())
    {
        handleServiceMessage(connId);
    }
}

void MECPerfApp::handleMp1Message(int connId)
{
    // for now I only have just one Service Registry
    HttpMessageStatus *msgStatus = (HttpMessageStatus*) mp1Socket_->getUserData();
    mp1HttpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();
    EV << "MECPerfApp::handleMp1Message - payload: " << mp1HttpMessage->getBody() << endl;

    try {
        nlohmann::json jsonBody = nlohmann::json::parse(mp1HttpMessage->getBody()); // get the JSON structure
        if (!jsonBody.empty()) {
            // found which service from the list is my at my Local Host
            int target = 0;
            int i = 0;
            while(i < jsonBody.size())
            {
                if(jsonBody[i]["isLocal"] == "TRUE")
                {
                    target = i;
                }
                i++;
            }
            jsonBody = jsonBody[target];
            std::string serName = jsonBody["serName"];
            if (serName.compare("LocationService") == 0) {
                if (jsonBody.contains("transportInfo")) {
                    nlohmann::json endPoint = jsonBody["transportInfo"]["endPoint"]["addresses"];
                    EV << "address: " << endPoint["host"] << " port: " << endPoint["port"] << endl;
                    std::string address = endPoint["host"];
                    serviceAddress_ = L3AddressResolver().resolve(address.c_str());
                    servicePort_ = endPoint["port"];
                    serviceSocket_ = addNewSocket();
                    connect(serviceSocket_ , serviceAddress_, servicePort_);
                }
            }
            else {
                EV << "MECPerfApp::handleMp1Message - LocationService not found" << endl;
                serviceAddress_ = L3Address();
            }
        }

        //close service registry socket
        mp1Socket_->close();

    }
    catch (nlohmann::detail::parse_error e) {
        EV << e.what() << std::endl;
        // body is not correctly formatted in JSON, manage it
        return;
    }
}

void MECPerfApp::handleServiceMessage(int connId)
{
    HttpBaseMessage *httpMessage = nullptr;
    HttpMessageStatus *msgStatus = (HttpMessageStatus*) serviceSocket_->getUserData();
    httpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();

    if (httpMessage == nullptr) {
        throw cRuntimeError("MECPerfApp::handleServiceMessage() - httpMessage is null!");
    }

    if (httpMessage->getType() == RESPONSE) {
        HttpResponseMessage *rspMsg = dynamic_cast<HttpResponseMessage*>(httpMessage);
        if (rspMsg->getCode() == 200) // in response to a successful GET request
        {
            EV << "MECPerfApp::handleServiceMessage - response 200 from Socket with Id [" << connId << "]" << endl;
            getRequestArrived_ = simTime();
            EV << "response time " << getRequestArrived_ - getRequestSent_ << endl;
            doComputation();
        }

        // some error occured, show the HTTP code for now
        else {
            EV << "MECPerfApp::handleServiceMessage - response with HTTP code:  " << rspMsg->getCode() << endl;
        }
    }
}

void MECPerfApp::doComputation()
{
    processingTime_ = vim->calculateProcessingTime(mecAppId, uniform(minInstructions_, maxInstructions_));
    EV << "time " << processingTime_ << endl;
    if(processingTimer_->isScheduled())
        cancelEvent(processingTimer_);
    scheduleAt(simTime()+ processingTime_, processingTimer_);
}

void MECPerfApp::sendGetRequest()
{
    //check if the ueAppAddress is specified
    if (serviceSocket_->getState() == inet::TcpSocket::CONNECTED) {
        EV << "MECPerfApp::sendGetRequest(): send request to the Location Service" << endl;
        std::stringstream uri;
        uri << "/example/location/v2/queries/users"; //TODO filter the request to get less data
        EV << "MECPerfApp::requestLocation(): uri: " << uri.str() << endl;
        std::string host = serviceSocket_->getRemoteAddress().str() + ":" + std::to_string(serviceSocket_->getRemotePort());
        Http::sendGetRequest(serviceSocket_, host.c_str(), uri.str().c_str());
    }
    else {
        EV << "MECPerfApp::sendGetRequest(): Location Service not connected" << endl;
    }
}

void MECPerfApp::established(int connId)
{
    EV << "MECPerfApp::established - connId [" << connId << "]" << endl;

    if (mp1Socket_ != nullptr && connId == mp1Socket_->getSocketId()) {
        EV << "MECPerfApp::established - Mp1Socket" << endl;

        // once the connection with the Service Registry has been established, obtain the
        // endPoint (address+port) of the Location Service
        const char *uri = "/example/mec_service_mgmt/v1/services?ser_name=LocationService";
        std::string host = mp1Socket_->getRemoteAddress().str() + ":" + std::to_string(mp1Socket_->getRemotePort());

        Http::sendGetRequest(mp1Socket_, host.c_str(), uri);
    }

}

void MECPerfApp::socketClosed(inet::TcpSocket *sock)
{
    EV << "MECPerfApp::socketClosed" << endl;
    std::cout << "MECPerfApp::socketClosed with sockId " << sock->getSocketId() << std::endl;
    if(mp1Socket_!= nullptr && sock->getSocketId() == mp1Socket_->getSocketId()){
        removeSocket(sock);
        mp1Socket_ = nullptr;
    }
    else
    {
        EV <<"Service socket closed" << endl;
        removeSocket(sock);
        sendStopAck();
    }

}

void MECPerfApp::sendStopAck()
{
    inet::Packet* pkt = new inet::Packet("RequestResponseAppPacket");
    auto req = inet::makeShared<RequestResponseAppPacket>();
    req->setType(UEAPP_ACK_STOP);;
    req->setChunkLength(B(packetSize_));
    pkt->insertAtBack(req);

    ueAppSocket_.sendTo(pkt, ueAppAddress, ueAppPort);
}

}
