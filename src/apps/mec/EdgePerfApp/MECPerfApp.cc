#include "apps/mec/EdgePerfApp/MECPerfApp.h"

#include "nodes/mec/utils/httpUtils/httpUtils.h"
#include "nodes/mec/utils/httpUtils/json.hpp"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpResponseMessage/HttpResponseMessage.h"

#include "apps/mec/EdgePerfApp/packets/RequestResponsePacket_m.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/Packet_m.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"

// include IPv4Header class
#include "inet/networklayer/ipv4/Ipv4Header_m.h"

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
    processingTimer_ = nullptr;
    numberOfRequests_ = 0;
}

MECPerfApp::~MECPerfApp()
{

    cancelAndDelete(currentProcessedMsg_);
    
    cancelAndDelete(processingTimer_);

     while(!requestQueue_.empty())
    {
        cancelAndDelete(requestQueue_.front()->requestMsg_);
        delete requestQueue_.front();
        requestQueue_.pop();
    }

    removeSocket(serviceSocket_);
    
    //if(serviceSocket_->getState() == inet::TcpSocket::CONNECTED)
    //    serviceSocket_->close();

    //std::cout << "APP IN MECHOST " << mecHost->getName() << " FINISHED" << std::endl;

    
}

void MECPerfApp::initialize(int stage)
{
    MecAppBase::initialize(stage);

    // avoid multiple initializations
    if (stage != inet::INITSTAGE_APPLICATION_LAYER)
        return;

    EV << "MECPerfApp::initialize - MEC application " << getClassName() << " with mecAppId[" << mecAppId << "] has started!" << endl;

    localUePort_ = par("localUePort");
    ueAppSocket_.setOutputGate(gate("socketOut"));
    ueAppSocket_.bind(localUePort_);
    ueAppSocket_.setDscp(8);
    processing_ = false;

    packetSize_ = par("responsePacketSize");

    processingTimer_  = new cMessage("computeMsg");

    minInstructions_ = par("minInstructions");
    maxInstructions_ = par("maxInstructions");

    // connect with the service registry
    EV << "MECPerfApp::initialize - Initialize connection with the Service Registry via Mp1" << endl;
    mp1Socket_ = addNewSocket();

    std::string mecHostName = mecHost->getName();
     //subtract mecHost from the name
    mecHostName = mecHostName.substr(7, mecHostName.length());

    mecHostId = stoi(mecHostName);

    //std::cout << "MECPerfApp::initialize - mecHostId: " << mecHostId << std::endl;

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
    if(gate("socketOut")->isConnected())
    {
        serviceSocket_->close();
        //std::cout << simTime() << " - MECPerfApp::finish - serviceSocket_ state" << serviceSocket_->getState() << std::endl;
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
    }
}

void MECPerfApp::handleRequest(cMessage* msg)
{
    // create new requestInfo
    requestInfo* reqInfo = new requestInfo();
    reqInfo->msgArrivedInfo_ = simTime();
    reqInfo->requestMsg_ = msg;
    requestQueue_.push(reqInfo);
    //std::cout << simTime() << " - Request Received! Size of requestQueue: " << requestQueue_.size() << std::endl;
    if(requestQueue_.size() == 1)
    {
        sendGetRequest();
    }
    else
    {
        return;
    }
    
}


void MECPerfApp::handleStopRequest(cMessage* msg)
{
    EV << "MECPerfApp::handleStopRequest" << endl;
    serviceSocket_->close();
}

void MECPerfApp::sendResponse()
{
    inet::Packet* packet = check_and_cast<inet::Packet*>(requestQueue_.front()->requestMsg_);
    ueAppAddress = packet->getTag<L3AddressInd>()->getSrcAddress();
    ueAppPort  = packet->getTag<L4PortInd>()->getSrcPort();

    auto req = packet->removeAtFront<RequestResponseAppPacket>(); 
    req->setType(MECAPP_RESPONSE);
    req->setMecHostId(mecHostId);
    req->setRequestArrivedTimestamp(requestQueue_.front()->msgArrivedInfo_);
    req->setServiceResponseTime(requestQueue_.front()->getRequestArrivedInfo_ - requestQueue_.front()->getRequestSentInfo_);
    req->setResponseSentTimestamp(simTime());
    req->setProcessingTime(requestQueue_.front()->processingTimeInfo_);
    req->setChunkLength(B(packetSize_));
    inet::Packet* pkt = new inet::Packet("ResponseAppPacket");
    pkt->insertAtBack(req);
    
    if(ueAppSocket_.getState() != inet::UdpSocket::CLOSED)
        ueAppSocket_.sendTo(pkt, ueAppAddress, ueAppPort);
    else
    {
        EV << "MECPerfApp::sendResponse - socket is not connected" << endl;
        return;
    }

    //clean current request
    delete packet;
    delete requestQueue_.front();
    requestQueue_.pop();
    //std::cout << simTime() << " - We poped a request from the queue! Size of requestQueue: " << requestQueue_.size() << std::endl;
    if(!requestQueue_.empty())
    {
        sendGetRequest();
    }
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
            requestQueue_.front()->getRequestArrivedInfo_ = simTime();
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
    requestQueue_.front()->processingTimeInfo_ = processingTime_;   
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
        //std::cout << "MECPerfApp::sendGetRequest"<< std::endl;    
        Http::sendGetRequest(serviceSocket_, host.c_str(), uri.str().c_str());
        // save the time when the request was sent into the oldest request in the queue
        requestQueue_.front()->getRequestSentInfo_ = simTime();
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
    //std::cout << "MECPerfApp::socketClosed with sockId " << sock->getSocketId() << std::endl;
    if(mp1Socket_!= nullptr && sock->getSocketId() == mp1Socket_->getSocketId()){
        removeSocket(sock);
        mp1Socket_ = nullptr;
    }
    else
    {
        EV <<"Service socket closed" << endl;
        removeSocket(sock);
        //sendStopAck();
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
