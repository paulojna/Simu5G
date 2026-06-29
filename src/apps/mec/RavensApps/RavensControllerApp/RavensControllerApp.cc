#include "RavensControllerApp.h"

#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "inet/transportlayer/contract/udp/UdpControlInfo_m.h"
#include "LocationDataHandlerPolicies/LocationDataHandlerPolicyBase.h"

#include "LocationDataHandlerPolicies/SaveDataHistory.h"
#include "LocationDataHandlerPolicies/NotifyOnDataChange.h"
#include "LocationDataHandlerPolicies/SendToExternalServer.h"

#define USERS_UPDATE 20
#define MIGRATION_PLAN 21
#define MAX_MEH_STATE_MAP_SIZE 15

namespace simu5g {

Define_Module(RavensControllerApp);


RavensControllerApp::RavensControllerApp(){
    locationDataHandlerPolicy_ = nullptr;
    calculateAvg_ = nullptr;
}

RavensControllerApp::~RavensControllerApp(){
    cancelAndDelete(calculateAvg_);
    delete locationDataHandlerPolicy_;
}

void RavensControllerApp::finish(){
    ApplicationBase::finish();
    if (udpSocket.isOpen())
        udpSocket.close();
    socketMap.deleteSockets();
}

void RavensControllerApp::initialize(int stage){
    ApplicationBase::initialize(stage);
    if (stage!=inet::INITSTAGE_APPLICATION_LAYER)
        return;
    snapshot_frequency_ = par("snapshot_frequency");
    snapshot_starting_time_ = par("snapshot_starting_time");
    threshold_ = par("threshold");
    confirmationCount_ = par("confirmationCount");
    exitConfidenceThreshold_ = par("exitConfidenceThreshold");
    frameInterval_ = par("frameInterval");
    update = nullptr;

    // start mehStateMap with a maximum size
    mehStateMap.reserve(MAX_MEH_STATE_MAP_SIZE);

    if(stage == inet::INITSTAGE_LOCAL){
        EV << "RavensControllerApp::initialize - stage " << stage << endl;
    }

    if(!strcmp(par("mode"), "SaveDataHistory")){
        EV << "RavensControllerApp::initialize - SaveDataHistory mode" << endl;
        locationDataHandlerPolicy_ = new SaveDataHistory(this, par("path"));
    }else if(!strcmp(par("mode"), "NotifyOnDataChange")){
        EV << "RavensControllerApp::initialize - NotifyOnDataChange handler mode" << endl;
        locationDataHandlerPolicy_ = new NotifyOnDataChange(this, par("threshold"));
    }else if(!strcmp(par("mode"), "SendToExternalServer")){
        EV << "RavensControllerApp::initialize - SendToExternalServer handler mode" << endl;
        locationDataHandlerPolicy_ = new SendToExternalServer(this);
    }else{
        throw cRuntimeError("RavensControllerApp::initialize - invalid mode parameter");
    }

    if(gate("outGate")->isConnected()){
        EV << "RavensControllerApp::initialize - outGate is connected" << endl;
    }else{
        EV << "RavensControllerApp::initialize - outGate is not connected" << endl;
    }

    // TODO: add feature to calculate network metrics
    //calculateAvg_ = new cMessage("calculateAvgNetworkData");

    ravensLinkPacketFilter.setPattern("RavensLink*");
    uePacketFilter.setPattern("User*");

    scheduleAt(simTime() + snapshot_starting_time_, new cMessage("sendSnapshot"));
    // scheduleAt(simTime() + 10, calculateAvg_);
}

void RavensControllerApp::handleMessageWhenUp(cMessage *msg){
    EV << "RavensControllerApp::handleMessage - new message received" << endl;
    if(msg->isSelfMessage()){
        handleSelfMessage(msg);
    } else if (udpSocket.belongsToSocket(msg)) {
        udpSocket.processMessage(msg);
    } else if (serverSocket_.belongsToSocket(msg)) {
        serverSocket_.processMessage(msg);
    } else if (auto *sock = socketMap.findSocketFor(msg)) {
        sock->processMessage(msg);
    } else {
        EV << "RavensControllerApp::handleMessageWhenUp - unknown message, dropping" << endl;
        delete msg;
    }
}

void RavensControllerApp::handleStartOperation(inet::LifecycleOperation *operation){
    EV << "RavensControllerApp::handleStartOperation - start operation" << endl;

    // UDP data socket — receives event frames and data frames
    int dataPort = par("dataPort");
    udpSocket.setOutputGate(gate("socketOut"));
    udpSocket.bind(dataPort);
    udpSocket.setCallback(this);

    // TCP mgmt server socket — accepts Agent config handshake connections
    int mgmtPort = par("mgmtPort");
    serverSocket_.setOutputGate(gate("socketOut"));
    serverSocket_.setCallback(this);
    serverSocket_.bind(mgmtPort);
    serverSocket_.listen();
    EV << "RavensControllerApp::handleStartOperation - TCP mgmt listening on port " << mgmtPort << endl;
}

void RavensControllerApp::handleStopOperation(inet::LifecycleOperation *operation){
    EV << "RavensControllerApp::handleStopOperation - start operation" << endl;
}

void RavensControllerApp::handleCrashOperation(inet::LifecycleOperation *operation){
    EV << "RavensControllerApp::handleCrashOperation - start operation" << endl;
}

void RavensControllerApp::handleSelfMessage(cMessage *msg){
    if(strcmp(msg->getName(), "sendSnapshot") == 0)
    {
        if(gate("outGate")->isConnected()){
            if(!userUpdates.empty())
            {
                // Convert map to vector for message interface
                std::vector<UserMEHUpdate> userUpdatesVector;
                userUpdatesVector.reserve(userUpdates.size());
                for (const auto& [address, update] : userUpdates) {
                    userUpdatesVector.push_back(update);
                }

                inet::Packet *update = new inet::Packet("UserMEHUpdatedListMessage");
                auto userMEHUpdatedListMessage = inet::makeShared<UserMEHUpdatedListMessage>();
                userMEHUpdatedListMessage->setChunkLength(inet::B(1500));
                userMEHUpdatedListMessage->setType(USERS_UPDATE);
                userMEHUpdatedListMessage->setUeMehList(userUpdatesVector);
                update->insertAtBack(userMEHUpdatedListMessage);
                send(update, "outGate");
                EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - report sent to MEO" << endl;

                //print userUpdates
                for (const auto& [address, user] : userUpdates) {
                    EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - user address: " << user.getAddress() << " last MEH: " << user.getLastMEHId() << " new MEH: " << user.getNewMEHId() << endl;
                }

                userUpdates.clear();
            }
            if(!migrationPredictions.empty())
            {
            	std::vector<MigrationPrediction> predictionsVector;
            	predictionsVector.reserve(migrationPredictions.size());
            	for (const auto& [address, prediction] : migrationPredictions) {
            		predictionsVector.push_back(prediction);
            	}

            	inet::Packet *entry = new inet::Packet("MigrationPredictionListMessage");
            	auto predictionListMessage = inet::makeShared<MigrationPredictionListMessage>();
            	predictionListMessage->setChunkLength(inet::B(1500));
            	predictionListMessage->setType(MIGRATION_PLAN);
            	predictionListMessage->setPredictions(predictionsVector);
            	entry->insertAtBack(predictionListMessage);
            	send(entry, "outGate");
            	EV << "RavensControllerApp::handleSelfMessage - sent " << predictionsVector.size() << " migration predictions to MEO" << endl;
            	migrationPredictions.clear();
            }
            if(userUpdates.empty() && migrationPredictions.empty()){
                EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - nothing to send" << endl;
            }
        }else{
            EV << "RavensControllerApp::handleSelfMessagw::sendSnapshot - outGate is not connected" << endl;
        }
        scheduleAt(simTime() + snapshot_frequency_, msg);
        EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - next snapshot scheduled" << endl;
    }
    else
    {
        EV << "RavensControllerApp::handleSelfMessage - unknown message" << endl;
    }
}

void RavensControllerApp::socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet){
    EV << "RavensControllerApp::socketDataArrived - socket data arrived" << endl;
    inet::L3Address remoteAddress = packet->getTag<inet::L3AddressInd>()->getSrcAddress();
    int srcPort = packet->getTag<inet::L4PortInd>()->getSrcPort();

    if(ravensLinkPacketFilter.matches(packet))
    {
        EV << "RavensControllerApp::socketDataArrived(UDP) - packet received" << endl;
        auto received_packet = packet->peekAtFront<RavensLinkPacket>();
        if(received_packet->getType() == DATA_FRAME)
        {
            auto dataFrame = packet->peekAtFront<RavensLinkDataFrameMessage>();
            updateUserStateMap(dataFrame);
            updateMehStateMap(dataFrame);
            locationDataHandlerPolicy_->handleDataMessage(dataFrame);
        }
        else if(received_packet->getType() == UE_EVENT)
        {
            auto eventMsg = packet->peekAtFront<RavensLinkEventMessage>();
            handleEventFrame(eventMsg, remoteAddress, srcPort);
        }
    	delete packet;
    }
    else{
        EV << "RavensControllerApp::socketDataArrived - unknown packet received" << endl;
    }
}

void RavensControllerApp::sendJoinNetworkAck(inet::TcpSocket *socket){
    EV << "RavensControllerApp::sendJoinNetworkAck - sending join network ack over TCP" << endl;
    inet::Packet* packet = new inet::Packet("JoinNetworkAckMessage");
    auto request = inet::makeShared<RavensLinkPacket>();
    request->setChunkLength(inet::B(500));
    request->setType(JOIN_NETWORK_ACK);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    packet->insertAtBack(request);
    socket->send(packet);
}

void RavensControllerApp::sendInfrastructureDetailsAck(inet::TcpSocket *socket){
    EV << "RavensControllerApp::sendInfrastructureDetailsAck - sending infrastructure details ack over TCP" << endl;
    inet::Packet* packet = new inet::Packet("RavensLinkInfrastructureDetailsAckMessage");
    auto request = inet::makeShared<RavensLinkInfrastructureDetailsMessageAck>();
    request->setChunkLength(inet::B(500));
    request->setType(INFRAESTRUCTURE_DETAILS_ACK);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    request->setInfoType(100);
    request->setRate((int)(frameInterval_ * 1000));
    int mode = !strcmp(par("mode"), "NotifyOnDataChange") ? AGENT_MODE_EVENT_ONLY : AGENT_MODE_EVENT_AND_DATA;
    request->setAgentMode(mode);
    packet->insertAtBack(request);
    socket->send(packet);
}

// --- TcpSocket::ICallback ---

void RavensControllerApp::socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *availableInfo){
    auto *clientSocket = new inet::TcpSocket(availableInfo);
    clientSocket->setOutputGate(gate("socketOut"));
    clientSocket->setCallback(this);
    socketMap.addSocket(clientSocket);
    socket->accept(availableInfo->getNewSocketId());
    EV << "RavensControllerApp::socketAvailable - accepted Agent connection" << endl;
}

void RavensControllerApp::socketEstablished(inet::TcpSocket *socket){
    EV << "RavensControllerApp::socketEstablished - Agent TCP connection established" << endl;
}

void RavensControllerApp::socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent){
    EV << "RavensControllerApp::socketDataArrived(TCP) - packet received" << endl;
    auto received_packet = packet->peekAtFront<RavensLinkPacket>();

    if(received_packet->getType() == JOIN_NETWORK_REQUEST){
        auto joinRequest = packet->peekAtFront<RavensLinkJoinNetworkRequestMessage>();
        EV << "RavensControllerApp::socketDataArrived(TCP) - JOIN_NETWORK_REQUEST from " << joinRequest->getMecHostId() << endl;
        MECHostData newHostData;
        newHostData.setHostId(joinRequest->getMecHostId());
        newHostData.setL3Address(socket->getRemoteAddress());
        mehStateMap[joinRequest->getMecHostId()] = newHostData;
        sendJoinNetworkAck(socket);
    }
    else if(received_packet->getType() == INFRAESTRUCTURE_DETAILS){
        auto infraDetails = packet->peekAtFront<RavensLinkInfrastructureDetailsMessage>();
        EV << "RavensControllerApp::socketDataArrived(TCP) - INFRAESTRUCTURE_DETAILS from " << infraDetails->getMecHostId() << endl;
        auto it = mehStateMap.find(infraDetails->getMecHostId());
        if(it == mehStateMap.end()){
            EV << "RavensControllerApp::socketDataArrived(TCP) - host " << infraDetails->getMecHostId() << " not found, ignoring" << endl;
            delete packet;
            return;
        }
        it->second.setAccessPoints(infraDetails->getAPList());
        sendInfrastructureDetailsAck(socket);
    }
    delete packet;
}

void RavensControllerApp::socketPeerClosed(inet::TcpSocket *socket){
    EV << "RavensControllerApp::socketPeerClosed - Agent closed TCP connection" << endl;
    socket->close();
}

void RavensControllerApp::socketClosed(inet::TcpSocket *socket){
    EV << "RavensControllerApp::socketClosed(TCP)" << endl;
    socketMap.removeSocket(socket);
    delete socket;
}

void RavensControllerApp::socketFailure(inet::TcpSocket *socket, int code){
    EV << "RavensControllerApp::socketFailure - code=" << code << endl;
    socketMap.removeSocket(socket);
    delete socket;
}

void RavensControllerApp::socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status){}

void RavensControllerApp::socketDeleted(inet::TcpSocket *socket){
    socketMap.removeSocket(socket);
}

void RavensControllerApp::handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event,
                                            inet::L3Address remoteAddress, int srcPort)
{
    // TODO Piece 10: confirm/reject handover based on event.samplesSinceChange vs confirmationCount_/exitConfidenceThreshold_
    EV << "RavensControllerApp::handleEventFrame - received " << event->getEvents().size() << " events from " << remoteAddress << endl;
    locationDataHandlerPolicy_->handleEventMessage(event);
}

void RavensControllerApp::socketClosed(inet::UdpSocket *socket){
    EV << "RavensControllerApp::socketClosed - socket closed" << endl;
}

void RavensControllerApp::socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication){
    EV << "RavensControllerApp::socketErrorArrived - socket error arrived" << endl;
}

/*
    Method that runs through the userStateMap and detect users that have not been updated for a pre-determined
    amount time - defined by the treshold_.
*/
std::vector<UserState> RavensControllerApp::removeInactiveUsers(){
    simtime_t actual = simTime();
    std::vector<UserState> inactiveUsers;

    auto it = userStateMap.begin();
    while (it != userStateMap.end()) {
        if(actual - it->second.timestamp > threshold_){
            // add the user to the list of inactive users
            inactiveUsers.push_back(it->second);
            it = userStateMap.erase(it);  // erase() returns iterator to next element
        } else {
            ++it;
        }
    }

    return inactiveUsers;
}

void RavensControllerApp::updateUserStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) {
    std::string hostId = received_packet->getMecHostId();

    for (const auto& [address, userData] : received_packet->getUsers()) {
        auto userIt = userStateMap.find(address);
        if (userIt == userStateMap.end()) {
            UserState state;
            state.userId = address;
            state.currentMEH = hostId;
            state.timestamp = received_packet->getTimeStamp();
            state.userData = userData;
            userStateMap[address] = state;
            EV << "RavensControllerApp::updateUserStateMap - New user " << address << " at " << hostId << endl;
        } else {
            if (received_packet->getTimeStamp() < userIt->second.timestamp) {
                EV << "RavensControllerApp::updateUserStateMap - Ignored stale update for user " << address << endl;
                continue;
            }
            // Refresh telemetry only — MEH transitions are driven by handleEventFrame()
            userIt->second.timestamp = received_packet->getTimeStamp();
            userIt->second.userData = userData;
        }
    }
}

/*
    Method to update the state of the mehStateMap. It receives a RavensLinkUsersInfoSnapshotMessage message,
    and updates the radio information for the corresponding MEC Host.
*/
void RavensControllerApp::updateMehStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) {
    auto usersInfoSnapshot = received_packet;
    std::string hostId = usersInfoSnapshot->getMecHostId();

    // Check if the snapshot contains AP Radio Info
    const AccessPointRadioInfoData& apRadioInfo = usersInfoSnapshot->getApRadioInfo();

    if (!apRadioInfo.getAccessPointId().empty()) {
        // Find the host in the map
        auto it = mehStateMap.find(hostId);
        if (it != mehStateMap.end()) {
            // Update the host's radio info
            it->second.setApRadioInfo(apRadioInfo);
            EV << "RavensControllerApp::updateMehStateMap - Updated Radio Info for host " << hostId << endl;
        } else {
            // Host not found (e.g., didn't join network yet)
            // We could choose to add it here, but typically we wait for JOIN_NETWORK_REQUEST.
            // For now, we just log a warning.
            EV << "RavensControllerApp::updateMehStateMap - WARNING: Received snapshot for unknown host " << hostId << endl;
        }
    } else {
        EV << "RavensControllerApp::updateMehStateMap - No valid AP Radio Info in snapshot for host " << hostId << endl;
    }
}


} // namespace


