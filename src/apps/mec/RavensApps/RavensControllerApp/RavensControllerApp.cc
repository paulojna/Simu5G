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
    if (udpSocket.isOpen()) {
        udpSocket.close();
    }
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
    }else{
        udpSocket.processMessage(msg);
    }
}

void RavensControllerApp::handleStartOperation(inet::LifecycleOperation *operation){
    EV << "RavensControllerApp::handleStartOperation - start operation" << endl;
    int port = par("dataPort");
    EV << "RavensControllerApp::initialize - binding to local port:" << port << endl;
    udpSocket.setOutputGate(gate("socketOut"));
    udpSocket.bind(port);
    udpSocket.setCallback(this);
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
        EV << "RavensControllerApp::socketDataArrived - ravens link packet received" << endl;
        // get the type of the message received
        auto received_packet = packet->peekAtFront<RavensLinkPacket>();
        if(received_packet->getType() == JOIN_NETWORK_REQUEST)
        {
            //cast the packet from RavensLinkPacket to the specific type of packet
            auto joinNetworkRequest = packet->peekAtFront<RavensLinkJoinNetworkRequestMessage>();

            EV << "RavensControllerApp::socketDataArrived - join network request received" << endl;

            // Create new MECHostState
            MECHostData newHostData;
            newHostData.setHostId(joinNetworkRequest->getMecHostId());
            newHostData.setL3Address(remoteAddress);
            newHostData.setPort(srcPort);

            mehStateMap[joinNetworkRequest->getMecHostId()] = newHostData;

            //send back a RAVENS_LINK_PACKET with type JOIN_NETWORK_ACK
            sendJoinNetworkAck(socket, remoteAddress, srcPort);
        }
        else if(received_packet->getType() == INFRAESTRUCTURE_DETAILS)
        {
            EV << "RavensControllerApp::socketDataArrived - infrastructure details received" << endl;

            auto infrastructureDetails = packet->peekAtFront<RavensLinkInfrastructureDetailsMessage>();

            // Get and log the AP list from the message
            std::vector<AccessPointData> apList = infrastructureDetails->getAPList();
            EV << "RavensControllerApp::socketDataArrived - Received APs for host " << infrastructureDetails->getMecHostId() << ":" << endl;
            for(const auto& ap : apList) {
                EV << "AP ID: " << ap.getAccessPointId() << endl;
            }

            // Find the MEC host in our state map
            auto it = mehStateMap.find(infrastructureDetails->getMecHostId());
            if(it == mehStateMap.end()) {
                EV << "RavensControllerApp::socketDataArrived - host " << infrastructureDetails->getMecHostId() << " not found" << endl;
                return;
            }

            // Update the host data
            it->second.setAccessPoints(apList);
            // Send acknowledgment
            sendInfrastructureDetailsAck(socket, remoteAddress, srcPort);

            EV << "RavensControllerApp::socketDataArrived - Updated infrastructure details for host " << it->first << ", now managing " << apList.size() << " access points" << endl;
        }
        else if(received_packet->getType() == DATA_FRAME)
        {
            auto dataFrame = packet->peekAtFront<RavensLinkDataFrameMessage>();
            updateUserStateMap(dataFrame);
            updateMehStateMap(dataFrame);
            // TODO Piece 11: update policy handler signature to accept RavensLinkDataFrameMessage
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

void RavensControllerApp::sendJoinNetworkAck(inet::UdpSocket *socket, inet::L3Address remoteAddress, int port){
    EV << "RavensControllerApp::sendJoinNetworkAck - sending join network ack" << endl;
    inet::Packet* packet = new inet::Packet("JoinNetworkAckMessage");
    auto request = inet::makeShared<RavensLinkPacket>();
    request->setChunkLength(inet::B(500));
    request->setType(JOIN_NETWORK_ACK);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    packet->insertAtBack(request);
    socket->sendTo(packet, remoteAddress, port);
}

void RavensControllerApp::sendInfrastructureDetailsAck(inet::UdpSocket *socket, inet::L3Address remoteAddress, int port){
    EV << "RavensControllerApp::sendInfrastructureDetailsAck - sending infrastructure details ack" << endl;
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
    socket->sendTo(packet, remoteAddress, port);
}

void RavensControllerApp::handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event,
                                            inet::L3Address remoteAddress, int srcPort)
{
    // TODO Piece 10: confirm/reject handover based on event.samplesSinceChange vs confirmationCount_/exitConfidenceThreshold_
    EV << "RavensControllerApp::handleEventFrame - received " << event->getEvents().size() << " events from " << remoteAddress << endl;
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


