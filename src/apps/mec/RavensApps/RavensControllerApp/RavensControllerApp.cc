#include "RavensControllerApp.h"

#include "Outputs/RavensOutputBase.h"
#include "Outputs/HistoryOutput.h"
#include "Outputs/MeoOutput.h"
#include "Outputs/PredictionOutput.h"

#define USERS_UPDATE 20
#define MIGRATION_PLAN 21

namespace simu5g {

Define_Module(RavensControllerApp);


RavensControllerApp::RavensControllerApp(){
    sendSnapshotMsg_ = nullptr;
    expireHoldsMsg_ = nullptr;
}

RavensControllerApp::~RavensControllerApp(){
    cancelAndDelete(sendSnapshotMsg_);
    cancelAndDelete(expireHoldsMsg_);
    for (auto* output : outputs_)
        delete output;
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
    staleWarningThreshold_ = par("staleWarningThreshold");
    frameInterval_ = par("frameInterval");

    // Profile = which outputs are active + which mode the Agents are put in.
    // HistoryOutput is registered first so ground truth is written before any
    // other output acts on the same frame.
    profile_ = par("profile").stringValue();
    if(profile_ == "History"){
        outputs_.push_back(new HistoryOutput(this, par("path"), "history"));
        agentMode_ = FULL_MODE;
    }else if(profile_ == "Prediction"){
        outputs_.push_back(new HistoryOutput(this, par("path"), "prediction"));
        outputs_.push_back(new MeoOutput(this));
        outputs_.push_back(new PredictionOutput(this));
        agentMode_ = FULL_MODE;
    }else if(profile_ == "Reaction"){
        outputs_.push_back(new MeoOutput(this));
        agentMode_ = EVENT_ONLY_MODE;
    }else{
        throw cRuntimeError("RavensControllerApp::initialize - invalid profile parameter '%s' (expected History, Prediction or Reaction)", profile_.c_str());
    }
    EV << "RavensControllerApp::initialize - profile " << profile_ << " with " << outputs_.size() << " output(s)" << endl;

    if(gate("outGate")->isConnected()){
        EV << "RavensControllerApp::initialize - outGate is connected" << endl;
    }else{
        EV << "RavensControllerApp::initialize - outGate is not connected" << endl;
    }

    ravensLinkPacketFilter.setPattern("RavensLink*");

    sendSnapshotMsg_ = new cMessage("sendSnapshot");
    scheduleAt(simTime() + snapshot_starting_time_, sendSnapshotMsg_);

    // Periodic F2 hold-expiry sweep. Decouples departure reporting from incoming
    // event frames: in quiet regions (and in EVENT_ONLY mode, which has no data
    // frames) a hold would otherwise never be revisited until some unrelated frame
    // arrived. Sweeps at frameInterval_ granularity (same scale as the hold itself).
    expireHoldsMsg_ = new cMessage("expireHolds");
    scheduleAt(simTime() + frameInterval_, expireHoldsMsg_);
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

    // UDP telemetry socket — receives TELEMETRY_FRAMEs (EVENT_FRAMEs are TCP-only)
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
    else if(strcmp(msg->getName(), "expireHolds") == 0)
    {
        // Frame-independent liveness: flush any F2 holds whose window has elapsed
        // even if no event frame has arrived to drive handleEventFrame().
        expirePendingExits();
        warnStaleUsers();
        scheduleAt(simTime() + frameInterval_, msg);
    }
    else
    {
        EV << "RavensControllerApp::handleSelfMessage - unknown message" << endl;
    }
}

void RavensControllerApp::socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet){
    EV << "RavensControllerApp::socketDataArrived - socket data arrived" << endl;

    if(ravensLinkPacketFilter.matches(packet))
    {
        EV << "RavensControllerApp::socketDataArrived(UDP) - packet received" << endl;
        auto received_packet = packet->peekAtFront<RavensLinkPacket>();
        if(received_packet->getType() == TELEMETRY_FRAME)
        {
            auto dataFrame = packet->peekAtFront<RavensLinkDataFrameMessage>();
            updateUserStateMap(dataFrame);
            updateMehStateMap(dataFrame);
            for (auto* output : outputs_)
                output->onTelemetry(dataFrame);
        }
        else
        {
            // EVENT_FRAMEs travel exclusively on the TCP signaling channel;
            // anything else on the telemetry port is a protocol violation.
            EV_WARN << "RavensControllerApp::socketDataArrived(UDP) - unexpected frame type "
                    << received_packet->getType() << " on telemetry port, dropping" << endl;
        }
    	delete packet;
    }
    else{
        EV << "RavensControllerApp::socketDataArrived - unknown packet received" << endl;
        delete packet;
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
    request->setType(INFRASTRUCTURE_DETAILS_ACK);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    request->setInfoType(100);
    request->setRate((int)(frameInterval_ * 1000));
    request->setAgentMode(agentMode_);  // derived from profile_ in initialize()
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

    // TCP is a byte stream: one delivery may carry several RavensLink messages
    // (e.g. a burst flushed after a retransmission) or a partial one. Push the
    // received bytes into this connection's reassembly queue and dispatch every
    // complete message; an incomplete tail stays queued for the next delivery.
    inet::ChunkQueue& queue = socketQueues_[socket->getSocketId()];
    queue.push(packet->peekDataAt(inet::B(0), packet->getTotalLength()));
    delete packet;

    while (queue.has<RavensLinkPacket>(inet::b(-1))) {
        auto received_packet = queue.pop<RavensLinkPacket>(inet::b(-1));

        if(received_packet->getType() == JOIN_NETWORK_REQUEST){
            auto joinRequest = inet::dynamicPtrCast<const RavensLinkJoinNetworkRequestMessage>(received_packet);
            EV << "RavensControllerApp::socketDataArrived(TCP) - JOIN_NETWORK_REQUEST from " << joinRequest->getMecHostId() << endl;
            MECHostData newHostData;
            newHostData.setHostId(joinRequest->getMecHostId());
            newHostData.setL3Address(socket->getRemoteAddress());
            mehStateMap[joinRequest->getMecHostId()] = newHostData;
            sendJoinNetworkAck(socket);
        }
        else if(received_packet->getType() == INFRASTRUCTURE_DETAILS){
            auto infraDetails = inet::dynamicPtrCast<const RavensLinkInfrastructureDetailsMessage>(received_packet);
            EV << "RavensControllerApp::socketDataArrived(TCP) - INFRASTRUCTURE_DETAILS from " << infraDetails->getMecHostId() << endl;
            auto it = mehStateMap.find(infraDetails->getMecHostId());
            if(it == mehStateMap.end()){
                EV << "RavensControllerApp::socketDataArrived(TCP) - host " << infraDetails->getMecHostId() << " not found, ignoring" << endl;
                continue;
            }
            it->second.setAccessPoints(infraDetails->getAPList());
            sendInfrastructureDetailsAck(socket);
        }
        else if(received_packet->getType() == EVENT_FRAME){
            // Event frames arrive over the reliable TCP signaling channel (report-once
            // semantics: a lost ENTRY/EXIT would corrupt placement state permanently).
            // Periodic TELEMETRY_FRAME telemetry stays on UDP.
            auto eventMsg = inet::dynamicPtrCast<const RavensLinkEventMessage>(received_packet);
            handleEventFrame(eventMsg);
        }
        else {
            EV_WARN << "RavensControllerApp::socketDataArrived(TCP) - unexpected frame type "
                    << received_packet->getType() << ", dropping" << endl;
        }
    }
}

void RavensControllerApp::socketPeerClosed(inet::TcpSocket *socket){
    EV << "RavensControllerApp::socketPeerClosed - Agent closed TCP connection" << endl;
    socket->close();
}

void RavensControllerApp::socketClosed(inet::TcpSocket *socket){
    EV << "RavensControllerApp::socketClosed(TCP)" << endl;
    socketQueues_.erase(socket->getSocketId());
    socketMap.removeSocket(socket);
    delete socket;
}

void RavensControllerApp::socketFailure(inet::TcpSocket *socket, int code){
    EV << "RavensControllerApp::socketFailure - code=" << code << endl;
    socketQueues_.erase(socket->getSocketId());
    socketMap.removeSocket(socket);
    delete socket;
}

void RavensControllerApp::socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status){}

void RavensControllerApp::socketDeleted(inet::TcpSocket *socket){
    socketMap.removeSocket(socket);
}

void RavensControllerApp::expirePendingExits()
{
    for (auto it = userStateMap.begin(); it != userStateMap.end(); ) {
        if (it->second.pendingExitTime != 0 && simTime() >= it->second.pendingExitTime) {
            EV << "RavensControllerApp::expirePendingExits - exit hold expired for "
               << it->first << ", removing" << endl;
            for (auto* output : outputs_)
                output->onUserExit(it->first, it->second.currentMEH,
                                   it->second.pendingExitSamples,
                                   it->second.pendingExitFirstAt);
            it = userStateMap.erase(it);
        } else {
            ++it;
        }
    }
}

void RavensControllerApp::handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event)
{
    std::string sourceMEH = event->getMecHostId();
    EV << "RavensControllerApp::handleEventFrame - " << event->getEvents().size()
       << " events from " << sourceMEH << endl;

    // Step 1: Expire any elapsed F2 holds promptly (the periodic timer is the
    // backstop for quiet periods; this catches them as soon as a frame arrives).
    expirePendingExits();

    // Step 2: Split events by type. No confidence gate — LS is the single source of
    // truth for presence, so every detected ENTRY/EXIT is acted on. Debounce
    // (handover vs. departure) is handled entirely by the F2 hold below, not by a
    // per-event sample threshold. samplesSinceChange / firstDetectedAt are carried
    // through to the output hooks as metadata only.
    RavensEventList entries, exits;
    for (const auto& e : event->getEvents()) {
        if (e.eventType == EVENT_ENTRY) entries.push_back(e);
        else                            exits.push_back(e);
    }

    // Step 3: Apply exits — start F2 hold window and stash metadata fields
    for (const auto& e : exits) {
        auto userIt = userStateMap.find(e.ueAddress);
        if (userIt == userStateMap.end())
            continue;
        // C5: ignore a stale EXIT from a MEH the user already left
        if (userIt->second.currentMEH != sourceMEH) {
            EV << "RavensControllerApp::handleEventFrame - stale EXIT for " << e.ueAddress
               << " from " << sourceMEH << " (current MEH is " << userIt->second.currentMEH
               << "), ignoring" << endl;
            continue;
        }
        userIt->second.pendingExitTime    = simTime() + frameInterval_;
        userIt->second.pendingExitSamples = e.samplesSinceChange;
        userIt->second.pendingExitFirstAt = e.firstDetectedAt;
        EV << "RavensControllerApp::handleEventFrame - EXIT hold started for "
           << e.ueAddress << ", expires at " << userIt->second.pendingExitTime << endl;
    }

    // Step 4: Apply entries — call hooks at each authoritative decision point
    for (const auto& e : entries) {
        auto userIt = userStateMap.find(e.ueAddress);
        if (userIt == userStateMap.end()) {
            // Brand-new user
            UserState state;
            state.userId              = e.ueAddress;
            state.currentMEH          = sourceMEH;
            state.timestamp           = simTime();
            state.pendingExitTime     = 0;
            state.pendingExitSamples  = 0;
            userStateMap[e.ueAddress] = state;
            for (auto* output : outputs_)
                output->onUserEntry(e.ueAddress, sourceMEH,
                                    e.samplesSinceChange, e.firstDetectedAt);
            EV << "RavensControllerApp::handleEventFrame - ENTRY new user "
               << e.ueAddress << " at " << sourceMEH << endl;
        } else if (userIt->second.pendingExitTime != 0) {
            // User was in exit hold — cancel it
            std::string fromMeh = userIt->second.currentMEH;
            userIt->second.pendingExitTime    = 0;
            userIt->second.pendingExitSamples = 0;
            userIt->second.timestamp          = simTime();
            if (sourceMEH != fromMeh) {
                // True handover: EXIT-from-A then ENTRY-at-B order
                userIt->second.currentMEH = sourceMEH;
                for (auto* output : outputs_)
                    output->onUserHandover(e.ueAddress, fromMeh, sourceMEH,
                                           e.samplesSinceChange, e.firstDetectedAt);
                EV << "RavensControllerApp::handleEventFrame - HANDOVER "
                   << e.ueAddress << " from " << fromMeh << " to " << sourceMEH << endl;
            }
            // else: re-entry at same MEH (flap) — cancel hold, no hook
        } else {
            // No active hold
            if (sourceMEH != userIt->second.currentMEH) {
                // ENTRY from a different MEH without a prior EXIT (ENTRY-before-EXIT order)
                std::string fromMeh = userIt->second.currentMEH;
                userIt->second.currentMEH = sourceMEH;
                for (auto* output : outputs_)
                    output->onUserHandover(e.ueAddress, fromMeh, sourceMEH,
                                           e.samplesSinceChange, e.firstDetectedAt);
                EV << "RavensControllerApp::handleEventFrame - HANDOVER (ENTRY-first) "
                   << e.ueAddress << " from " << fromMeh << " to " << sourceMEH << endl;
            }
            // else: duplicate ENTRY at same MEH — no hook
            userIt->second.timestamp = simTime();
        }
    }
}

void RavensControllerApp::socketClosed(inet::UdpSocket *socket){
    EV << "RavensControllerApp::socketClosed - socket closed" << endl;
}

void RavensControllerApp::socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication){
    EV << "RavensControllerApp::socketErrorArrived - socket error arrived" << endl;
}

/*
    Diagnostic sweep (called from the periodic expireHolds self-message).

    Departures always arrive as reliable EXIT events over TCP, so a user whose
    state stops being refreshed can only mean a pipeline bug — it is reported
    loudly (once per user) but state is never mutated. Only meaningful when
    telemetry flows (FULL mode): in EVENT_ONLY mode a stable user is legitimately
    silent, so the check is skipped entirely.
*/
void RavensControllerApp::warnStaleUsers(){
    if (agentMode_ != FULL_MODE)
        return;

    simtime_t now = simTime();
    for (auto& [address, state] : userStateMap) {
        if (!state.staleWarned && now - state.timestamp > staleWarningThreshold_) {
            EV_WARN << "RavensControllerApp::warnStaleUsers - user " << address
                    << " at " << state.currentMEH << " has had no update for "
                    << (now - state.timestamp) << "s (threshold " << staleWarningThreshold_
                    << "s) — possible pipeline bug, state kept" << endl;
            state.staleWarned = true;
        }
    }
}

void RavensControllerApp::updateUserStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) {
    std::string hostId = received_packet->getMecHostId();

    for (const auto& [address, userData] : received_packet->getUsers()) {
        auto userIt = userStateMap.find(address);
        if (userIt == userStateMap.end()) {
            // User not yet confirmed by an ENTRY event — skip.
            // Presence is authoritative only from handleEventFrame(); TELEMETRY_FRAME
            // must not pre-empt the ENTRY or onUserEntry will never fire.
            EV << "RavensControllerApp::updateUserStateMap - TELEMETRY_FRAME for unknown user "
               << address << ", skipping (waiting for ENTRY event)" << endl;
            continue;
        }
        if (received_packet->getTimeStamp() < userIt->second.timestamp) {
            EV << "RavensControllerApp::updateUserStateMap - Ignored stale update for user " << address << endl;
            continue;
        }
        // Refresh telemetry only — MEH transitions are driven by handleEventFrame()
        userIt->second.timestamp = received_packet->getTimeStamp();
        userIt->second.userData = userData;
        userIt->second.staleWarned = false;   // fresh again — re-arm the stale warning
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


