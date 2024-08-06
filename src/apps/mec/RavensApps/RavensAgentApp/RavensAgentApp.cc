#include "RavensAgentApp.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/Packet_m.h"

#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"

#include "nodes/mec/utils/httpUtils/httpUtils.h"
#include "nodes/mec/utils/httpUtils/json.hpp"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpRequestMessage/HttpRequestMessage.h"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpResponseMessage/HttpResponseMessage.h"

#include <map>

using namespace inet;
Define_Module(RavensAgentApp);

RavensAgentApp::RavensAgentApp(): MecAppBase()
{
    std::map<std::string, int> m;
    this->sendInterval = 1; // default value
    this->localSnapshotCounter = 0;
}

RavensAgentApp::~RavensAgentApp()
{
    cancelAndDelete(userList);
}

void RavensAgentApp::initialize(int stage)
{
    MecAppBase::initialize(stage);

    //avoiding multiple initializations
    if(stage != inet::INITSTAGE_APPLICATION_LAYER)
        return;

    EV << "RavensAgentApp::initialize - Mec application "<< getClassName() << " with mecAppId["<< mecAppId << "] has started! " <<  endl;
    EV << "RavensAgentApp::initialize - Initializing MecAppBase variables such as the mp1Port -->" << mp1Port << endl;
    mp1Socket_ = addNewSocket();

    controllerPort = par("controllerPort");
    localPort_ = par("localPort");

    userList = new cMessage("userList");
    userLocation = new cMessage("userLocation");

    accessPoints = std::vector<AccessPointData>();
    users = std::map<std::string, UserData>();

    this->mecHostId = mecHost->getName();

    //define a file with the name of the mec_host -> not necessary anymore
    /*
    std::string host = getParentModule()->getFullName();
    std::string name = host+".csv";
    myfile.open (name, std::ios_base::app);
    */

    cMessage *msg = new cMessage("connectRC");
    scheduleAt(simTime() + 0.5, msg);
}

void RavensAgentApp::finish()
{
    MecAppBase::finish();
    EV << "RavensAgentApp::finish()" << endl;

    if(gate("socketOut")->isConnected()){

    }
}

void RavensAgentApp::established(int connId)
{
    if(connId == mp1Socket_->getSocketId())
    {
        EV << "RavensAgentApp::established - Mp1Socket" << endl;

        // get endpoint for the location service
        const char *location_service_uri = "/example/mec_service_mgmt/v1/services?ser_name=LocationService";
        std::string host = mp1Socket_->getRemoteAddress().str()+":"+std::to_string(mp1Socket_->getRemotePort());
        Http::sendGetRequest(mp1Socket_, host.c_str(), location_service_uri);
        EV << "RavensAgentApp::established - Request Sent to " << host.c_str() << " to " << location_service_uri << endl;
        return;
    }
    else if (connId == lsSocket_->getSocketId())
    {
        EV << "RavensAgentApp::established - lsSocket"<< endl;
        sendAPListRequest();
        return;
    }
    else 
    {
        throw cRuntimeError("RavenAgentApp::socketEstablished - Socket %d not recognized", connId);
    }
}

void RavensAgentApp::sendJoinNetworkRequest()
{
    EV << "RavensAgentApp::sendJoinNetworkRequest - Sending Join Network Request" << endl;
    inet::Packet* packet = new inet::Packet("JoinNetworkRequestMessage");
    auto request = inet::makeShared<JoinNetworkRequestMessage>();
    request->setChunkLength(B(500));
    request->setType(JOIN_NETWORK_REQUEST);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    // send mechostid from the mecHost of the MecAppBase
    request->setMecHostId(getMecHostId().c_str());
    packet->insertAtBack(request);
    controllerSocket_.send(packet);
}

void RavensAgentApp::sendAPList()
{
    EV << "RavensAgentApp::sendAPList - Sending AP List" << endl;
    inet::Packet* packet = new inet::Packet("InfrastructureDetailsMessage");
    auto request = inet::makeShared<InfrastructureDetailsMessage>();
    request->setChunkLength(B(500));
    request->setType(INFRAESTRUCTURE_DETAILS);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    request->setMecHostId(getMecHostId().c_str());
    request->setAPList(accessPoints);
    packet->insertAtBack(request);
    controllerSocket_.send(packet);
}

void RavensAgentApp::sendUsersInfoSnapshot()
{
    // get the information available on the users map and send it to the controller using the message type USER_INFO_SNAPSHOT
    EV << "RavensAgentApp::sendUsersInfoSnapshot - Sending User Info Snapshot" << endl;
    inet::Packet* packet = new inet::Packet("UsersInfoSnapshotMessage");
    auto request = inet::makeShared<UsersInfoSnapshotMessage>();
    request->setChunkLength(B(500));
    request->setType(USERS_INFO_SNAPSHOT);
    request->setRequestId(localSnapshotCounter);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    request->setMecHostId(getMecHostId().c_str());
    request->setUsers(users);
    packet->insertAtBack(request);
    controllerSocket_.send(packet);
    localSnapshotCounter++;

    // schedule the next snapshot to send
    cMessage *msg = new cMessage("sendUserList");
    EV << "RavensAgentApp::sendUsersInfoSnapshot - Next snapshot scheduled in " << getRetrievalInterval() << " seconds" << endl;
    simtime_t interval = simTime() + getRetrievalInterval();
    scheduleAt(interval, msg);
}

void RavensAgentApp::handleMp1Message(int connId)
{
    HttpMessageStatus *msgStatus = (HttpMessageStatus*) mp1Socket_->getUserData();
    mp1HttpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();

    EV << "RavensAgentApp::handleMp1Message - payload: " << mp1HttpMessage->getBody() << endl;

    try
    {
        nlohmann::json jsonBody = nlohmann::json::parse(mp1HttpMessage->getBody()); // get the JSON structure
        if(!jsonBody.empty())
        {
            // find which service from the list is running at my Local Host
            int target = 0;
            int i = 0;
            std::cout << jsonBody << std::endl;
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
            if(serName.compare("LocationService") == 0)
            {
                if(jsonBody.contains("transportInfo"))
                {
                    nlohmann::json endPoint = jsonBody["transportInfo"]["endPoint"]["addresses"];
                    EV << "address: " << endPoint["host"] << " port: " <<  endPoint["port"] << endl;
                    std::string address = endPoint["host"];
                    serviceAddress = L3AddressResolver().resolve(address.c_str());;
                    servicePort = endPoint["port"];
                    lsSocket_ = addNewSocket();
                    cMessage *m = new cMessage("connectLS");
                    scheduleAt(simTime()+0, m);
                }
            } else 
            {
                EV << "RavensAgentApp::handleMp1Message - Location Service not found"<< endl;
                serviceAddress = L3Address();
            }
        }

    }
    catch (nlohmann::detail::parse_error &e)
    {
        EV <<  e.what() << std::endl;
        // body is not correctly formatted in JSON, manage it
        return;
    }
}

void RavensAgentApp::handleHttpMessage(int connId)
{
    EV << "RavensAgentApp::handleHttpMessage - Http Message Received" <<  connId << endl;
    if (mp1Socket_ != nullptr && connId == mp1Socket_->getSocketId())
    {
        handleMp1Message(connId);
    }
    else if (lsSocket_ != nullptr && connId == lsSocket_->getSocketId())
    {
        handleLSMessage(connId);
    }
}

void RavensAgentApp::handleSelfMessage(cMessage *msg)
{
    if(strcmp(msg->getName(), "connectMp1") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        connect(mp1Socket_, mp1Address, mp1Port);
        delete msg;
    }
    else if(strcmp(msg->getName(), "connectLS") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        if(!serviceAddress.isUnspecified() && lsSocket_->getState() != inet::TcpSocket::CONNECTED)
        {
            connect(lsSocket_, serviceAddress, servicePort);
        }
        else
        {
            if(serviceAddress.isUnspecified())
                EV << "RavensAgentApp::handleSelfMessage - Location service IP address is  unspecified (maybe response from the service registry is arriving)" << endl;
            else if(lsSocket_->getState() == inet::TcpSocket::CONNECTED)
                EV << "RavensAgentApp::handleSelfMessage - Location service socket is already connected" << endl;
        }
        delete msg;
    }
    else if(strcmp(msg->getName(), "connectRC") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        connectToRavensController();
        sendJoinNetworkRequest();
        delete msg;
    }
    else if(strcmp(msg->getName(), "sendAPDetails") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        // send AP information to RavensController and get the information about the type of information we want to get from the location service and with which frequency
        sendAPList();
        delete msg;
    }
    else if(strcmp(msg->getName(), "sendUserListSub") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        sendUsersListSubscription();
        delete msg;
    }
    else if(strcmp(msg->getName(), "sendUserList") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        sendUsersInfoSnapshot();
        delete msg;
    }
    else
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
    }
}

void RavensAgentApp::connectToRavensController()
{
    cMessage *msg = new cMessage("connectRC");

    std::string controller = par("controllerAddress").stringValue();
    cModule* controllerModule = findModuleByPath(par("controllerAddress").stringValue());

    EV << "RAVENS CONTROLLER ADDRESS: " << controller.c_str() << endl;

    if (controllerModule == nullptr) {
        EV << "RavensAgentApp::connectToRavensController - " << controller << " not found" << endl;
        scheduleAt(simTime() + 0.05, msg);
        EV << "RavensAgentApp::connectToRavensController - the node will retry to connect to the controller in 0.05 seconds " << endl;
    }
    else {
        delete msg;
        controllerSocket_.setOutputGate(gate("socketOut"));
        controllerSocket_.bind(localPort_);
        controllerSocket_.setCallback(this);
        
        controllerAddress_ = L3AddressResolver().resolve(par("controllerAddress")); // ravensController
        
        EV << "Connecting to " << controllerAddress_ << " port=" << controllerPort << endl;
        
        controllerSocket_.connect(controllerAddress_, controllerPort);    
    } 
}

void RavensAgentApp::handleLSMessage(int connId)
{
    EV << "RavensAgentApp::handleLSMessage - LS Message Received - Socket ID: " << connId << endl;

    HttpMessageStatus *msgStatus = (HttpMessageStatus*) lsSocket_->getUserData();
    serviceHttpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();
    HttpResponseMessage *rspMsg = dynamic_cast<HttpResponseMessage*>(serviceHttpMessage);

    int code = rspMsg->getCode();

    EV << "RavensAgentApp::handleLSMessage - LS Message payload with code " << code << " received: " <<  serviceHttpMessage->getBody() << endl;

    // clean users vector
    users.clear();

    // if the response is a 200 OK
    if(code == 200)
    {
        // get the JSON structure
        nlohmann::json jsonBody = nlohmann::json::parse(serviceHttpMessage->getBody());
        if(!jsonBody.empty())
        {
            // find if the json contains the cellList and fill the accessPoints vector
            if(jsonBody.contains("cellList"))
            {
                nlohmann::json cellList = jsonBody["cellList"];
                for (auto& cell : cellList)
                {
                    std::string cellId = to_string(cell["cellId"]);
                    long x = cell["position"]["x"];
                    long y = cell["position"]["y"];
                    NodeLocation apLocation = NodeLocation(x, y, 0);
                    AccessPointData apData = AccessPointData(cellId, apLocation);
                    accessPoints.push_back(apData);
                }
                // send the information we were just given to the RavensController
                cMessage *msg = new cMessage("sendAPDetails");
                scheduleAt(simTime() + 0.5, msg);
            }
            else if(jsonBody.contains("subscriptionNotification"))
            {
                nlohmann::json userInfoList = jsonBody["subscriptionNotification"]["userInfoList"];
                for (auto& user : userInfoList)
                {
                    std::string address = user["userInfo"]["address"];
                    std::string accessPointId = to_string(user["userInfo"]["accessPointId"]);
                    // get accessPointData from accessPoints vector
                    AccessPointData apData;
                    for (auto& ap : accessPoints)
                    {
                        if(ap.getAccessPointId().compare(accessPointId) == 0)
                        {
                            apData = ap;
                            break;
                        }
                    }
                    EV << "X" << endl;
                    long x = user["userInfo"]["locationInfo"]["x"];
                    long y = user["userInfo"]["locationInfo"]["y"];
                    long bearing = user["userInfo"]["locationInfo"]["velocity"]["bearing"];
                    long speed = user["userInfo"]["locationInfo"]["velocity"]["horizontalSpeed"];
                    UserLocation userLocation = UserLocation(x, y, 0, bearing, speed);
                    UserData userData = UserData(address, apData, userLocation);
                    users[address] = userData;
                }
                // add users to history
                // history.emplace(userInfoList["timeStamp"], users);

                // run through users
                /*for (auto& user : users)
                {
                    // send user data to myfile
                    myfile << to_string(jsonBody["subscriptionNotification"]["timeStamp"]) << "," << user.first << "," << user.second.getAccessPointId() << "," << user.second.getCurrentLocation().getX() << "," << user.second.getCurrentLocation().getY() << "," << to_string(user.second.getDistanceToAP()) << "," << to_string(user.second.getCurrentLocation().getHorizontalSpeed()) << endl;
                }*/ 
            }
        }
    }
    else if(code == 201)
    {
        // it means that we sucefully subscribed to the users/list uri and we can start sending user list messages to the controller
        cMessage *msg = new cMessage("sendUserList");
        scheduleAt(simTime() + 1, msg);
    }
    else
    {
        EV << "RavensAgentApp::handleLSMessage - LS Message payload with code " << code << " received: " <<  serviceHttpMessage->getBody() << endl;
    }

    // print users
    for (auto& user : users)
    {
        EV << "RavensAgentApp::handleLSMessage - User: " << user.first << " AccessPoint: " << user.second.getAccessPointId() << endl;
    }
}

void RavensAgentApp::handleProcessedMessage(cMessage *msg)
{
    EV << "RavensAgentApp::handleProcessedMessage - Message Received" <<  msg->getName() << endl;
    // check if the message is from the RavensController
    if(controllerSocket_.belongsToSocket(msg))
    {
        inet::Packet* packet = nullptr;
        try {
            packet = check_and_cast<inet::Packet*>(msg);
            auto received_packet = packet->peekAtFront<RavensLinkPacket>();
            EV << "RavensAgentApp::handleProcessedMessage: received message from Ravens Controller" << endl;
            if(received_packet->getType() == JOIN_NETWORK_ACK)
            {
                EV << "RavensAgentApp::handleProcessedMessage - JOIN_NETWORK_ACK received" << endl;
                cMessage *msg = new cMessage("connectMp1");
                scheduleAt(simTime() + 0, msg);
            }
            else if(received_packet->getType() == INFRAESTRUCTURE_DETAILS_ACK)
            {
                EV << "RavensAgentApp::handleProcessedMessage - INFRAESTRUCTURE_DETAILS received" << endl;
                auto infrastructureDetailsAck = packet->peekAtFront<InfrastructureDetailsMessageAck>();
                EV << "RavensAgentApp::handleProcessedMessage - Rate received: " << infrastructureDetailsAck->getRate() << endl;
                simtime_t interval = infrastructureDetailsAck->getRate();
                // convert to int
                int intervalInt = (int) interval.dbl();  
                setRetrievalInterval(intervalInt/1000);
                cMessage *msg = new cMessage("sendUserListSub");
                scheduleAt(simTime() + 0, msg);
            }
        } catch (const cRuntimeError& err)
        {
            std::cerr << "received uncastable msg with name " << msg->getName() << " of class " << msg->getClassName() << std::endl;
            delete msg;
            return;
        }
    }
    else{
        MecAppBase::handleProcessedMessage(msg);
    }
}

void RavensAgentApp::sendUsersListSubscription()
{
    EV << "RavensAgentApp::sendUsersListSubscription - Sending users/list Subscription" << endl;
    std::string body = "{  \"usersListNotificationSubscription\": {"
                           "\"callbackReference\" : {"
                            "\"callbackData\":\"v0\","
                            "\"notifyURL\":\"ravens.user.list\"},"
                           "\"checkImmediate\": \"true\","
                            "\"frequency\": 0.5,"
                            "\"cells\": [0]"
                            "}"
                            "}\r\n";
    std::string uri = "/example/location/v2/subscriptions/users/list";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());

    Http::sendPostRequest(lsSocket_, body.c_str(), host.c_str(), uri.c_str());
}

void RavensAgentApp::sendUsersDensitySubscription()
{
    EV << "RavensAgentApp::sendUsersDensitySubscription - Sending users/density Subscription" << endl;
    std::string body = "{  \"usersDensityNotificationSubscription\": {"
                           "\"callbackReference\" : {"
                            "\"callbackData\":\"v0\","
                            "\"notifyURL\":\"ravens.user.density\"},"
                           "\"checkImmediate\": \"false\","
                            "\"frequency\": 5,"
                            "\"cells\": [0]"
                            "}"
                            "}\r\n";
    std::string uri = "/example/location/v2/subscriptions/users/density";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());

    Http::sendPostRequest(lsSocket_, body.c_str(), host.c_str(), uri.c_str());
}

void RavensAgentApp::sendUserListRequest()
{
    const char *users_uri = "/example/location/v2/queries/users";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());
    Http::sendGetRequest(lsSocket_, host.c_str(), users_uri);
    EV << "RavensAgentApp::sendUserListRequest - uri " << users_uri << " to host " << host.c_str() << endl;
    return;
}

void RavensAgentApp::sendAPListRequest()
{
    const char *zones_uri = "/example/location/v2/queries/accessPoints";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());
    Http::sendGetRequest(lsSocket_, host.c_str(), zones_uri);
    EV << "RavensAgentApp::sendAPListRequest - uri " << zones_uri << " to host " << host.c_str() << endl;
    return;
}

std::string RavensAgentApp::collectionString(std::vector<std::string> vec)
{
    std::string userListString = "";
    std::stringstream ss;
    for (size_t i = 0; i < vec.size(); i++) {
        if (i != 0) {
            ss << ",";
        }
        ss << vec[i];
    }
    return ss.str();
}

void RavensAgentApp::handleServiceMessage(int connId)
{
    EV << "RavensAgentApp::handleServiceMessage - Service Message Received" <<  connId << endl;
}

void RavensAgentApp::handleUeMessage(cMessage *msg)
{
    EV << "Message from UE" << endl;
}

void RavensAgentApp::socketDataArrived(UdpSocket *socket, inet::Packet *packet){
    EV << "RavensAgentApp::socketDataArrived - socketDataArrived FROM RavensControllerApp" << endl;
}

void RavensAgentApp::socketErrorArrived(UdpSocket *socket, inet::Indication *indication) {
    EV << "RavensAgentApp::socketErrorArrived - socketErrorArrived" << endl;
}

void RavensAgentApp::socketClosed(UdpSocket *socket){
    EV << "RavensAgentApp::socketClosed - socketClosed" << endl;
}

simtime_t RavensAgentApp::getRetrievalInterval()
{
    return sendInterval;
}

void RavensAgentApp::setRetrievalInterval(simtime_t interval)
{
    sendInterval = interval;
}

std::string RavensAgentApp::getMecHostId()
{
    return mecHostId;
}
