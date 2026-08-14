#include "RavensControllerApp.h"

#include "apps/mec/RavensApps/RavensLinkSizes.h"

// The Controller derives its duties from what the orchestrator consumes; see
// the derivation block in initialize().
#include "nodes/mec/MECOrchestrator/MecOrchestrator.h"

#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "inet/transportlayer/contract/udp/UdpControlInfo_m.h"

#include "TelemetrySinks/TelemetrySink.h"
#include "TelemetrySinks/CsvEventRecorder.h"
#include "TelemetrySinks/CsvTelemetryRecorder.h"
#include "TelemetrySinks/PredictionServerClient.h"

#define MAX_MEH_STATE_MAP_SIZE 15

namespace simu5g {

Define_Module(RavensControllerApp);

// Every message to the orchestrator needs a chunk length, and none of them have
// a meaningful one: the link is a direct module connection with no channel,
// delay or datarate, so nothing here is ever serialised or transmitted. See the
// header comment in RavensControlPacket.msg — these bytes must not be added to a
// signaling overhead total.
static inet::B controlLinkChunkLength() { return inet::B(1500); }

RavensControllerApp::RavensControllerApp(){
    closeTelemetryWindowMsg_ = nullptr;
    expireHoldsMsg_ = nullptr;
}

RavensControllerApp::~RavensControllerApp(){
    cancelAndDelete(closeTelemetryWindowMsg_);
    cancelAndDelete(expireHoldsMsg_);
}

void RavensControllerApp::finish(){
    ApplicationBase::finish();

    // Expected to be zero. A non-zero value means a placement change went
    // missing on a channel that is supposed to make that impossible — see
    // reportSilentUsers().
    recordScalar("silentUsersReported", silentUsersReported_);

    // What this run's duties were derived to be. A results directory is named
    // for what the run was *meant* to be; these scalars are the run's own
    // record of what it actually did, so a derivation gone wrong is caught
    // from the output instead of trusted from the directory name.
    recordScalar("derivedCollectTelemetry", collectTelemetry_ ? 1 : 0);
    recordScalar("derivedCallModelServer", callModelServer_ ? 1 : 0);

    if (udpSocket.isOpen())
        udpSocket.close();
    socketMap.deleteSockets();
}

void RavensControllerApp::initialize(int stage){
    ApplicationBase::initialize(stage);
    if (stage!=inet::INITSTAGE_APPLICATION_LAYER)
        return;
    silenceWarningThreshold_ = par("silenceWarningThreshold");
    telemetryInterval_ = par("telemetryInterval");
    exitConfirmationWindow_ = par("exitConfirmationWindow");
    telemetryWindow_ = par("telemetryWindow");

    // start mehStateMap with a maximum size
    mehStateMap.reserve(MAX_MEH_STATE_MAP_SIZE);

    if(stage == inet::INITSTAGE_LOCAL){
        EV << "RavensControllerApp::initialize - stage " << stage << endl;
    }

    std::string predictionServerUrl = par("predictionServerUrl").stdstringValue();
    bool recordTelemetry = par("recordTelemetry");

    // Whether the Agents send telemetry is derived, not configured: they send
    // exactly when something downstream consumes it. Every possible consumer
    // is known right here — the telemetry recorder, the model server (called
    // exactly when the orchestrator's strategy consumes predictions), or the
    // orchestrator itself. The orchestrator is the authority: it is asked what
    // it consumes, and the Controller's duties and the Agents' mode follow. A
    // run whose strategy needs predictions while its Agents send nothing to
    // predict from can no longer be written, so the startup check that used to
    // catch that combination is gone along with the switch that allowed it.
    //
    // This stays the expensive switch, and the only one that changes the
    // simulated network: telemetry frames cross the modelled link, so a run
    // with them off has genuinely less signaling traffic, not merely less
    // bookkeeping.
    bool orchestratorWantsTelemetry = false;
    if (gate("outGate")->isConnected()) {
        auto *orchestrator = check_and_cast<MecOrchestrator *>(
            gate("outGate")->getPathEndGate()->getOwnerModule());
        callModelServer_ = orchestrator->consumesPredictions();
        orchestratorWantsTelemetry = orchestrator->consumesTelemetry();
    }
    else {
        EV_WARN << "RavensControllerApp::initialize - outGate is not connected: no "
                << "orchestrator downstream, deriving from the recording duties alone" << endl;
    }
    collectTelemetry_ = recordTelemetry || callModelServer_ || orchestratorWantsTelemetry;
    agentMode_ = collectTelemetry_ ? FULL_MODE : EVENT_ONLY_MODE;

    EV << "RavensControllerApp::initialize - derived duties: collectTelemetry=" << collectTelemetry_
       << " (recordTelemetry=" << recordTelemetry << ", callModelServer=" << callModelServer_
       << ", orchestratorConsumesTelemetry=" << orchestratorWantsTelemetry << "); Agents in "
       << (collectTelemetry_ ? "full" : "event-only") << " mode" << endl;

    // The one input the derivation cannot supply: a strategy that consumes
    // predictions needs a server address to get them from.
    if (callModelServer_ && predictionServerUrl.empty())
        throw cRuntimeError("RavensControllerApp::initialize - the orchestrator's strategy "
                            "consumes predictions but predictionServerUrl is empty");

    // Events are cheap to record and are what a run is judged by, so this stays
    // on in configurations that record no telemetry at all.
    if (par("recordEvents").boolValue()) {
        EV << "RavensControllerApp::initialize - recording events to " << par("path").stringValue() << endl;
        telemetrySinks_.push_back(std::make_unique<CsvEventRecorder>(this, par("path")));
    }
    if (recordTelemetry) {
        EV << "RavensControllerApp::initialize - recording telemetry to " << par("path").stringValue() << endl;
        telemetrySinks_.push_back(std::make_unique<CsvTelemetryRecorder>(this, par("path")));
    }
    if (callModelServer_) {
        EV << "RavensControllerApp::initialize - prediction server at " << predictionServerUrl << endl;
        telemetrySinks_.push_back(std::make_unique<PredictionServerClient>(this, predictionServerUrl));
    }

    ravensLinkPacketFilter.setPattern("RavensLink*");
    uePacketFilter.setPattern("User*");

    // The telemetry window. Nothing else waits for a tick — user events and
    // predictions go out the moment they exist — but a learning agent wants one
    // coherent round of observations more than it wants them a second sooner.
    if (collectTelemetry_) {
        windowStart_ = simTime();
        closeTelemetryWindowMsg_ = new cMessage("closeTelemetryWindow");
        scheduleAt(simTime() + telemetryWindow_, closeTelemetryWindowMsg_);
    }

    // Periodic sweep for exit windows that have elapsed. Decouples departure
    // reporting from incoming event frames: in quiet regions (and with telemetry
    // off, where there are no telemetry frames at all) a waiting exit would
    // otherwise never be revisited until some unrelated frame arrived.
    // Sweeping at half the window means an exit is confirmed between one and one
    // and a half windows after it was reported, instead of up to two.
    expireHoldsMsg_ = new cMessage("expireHolds");
    scheduleAt(simTime() + exitConfirmationWindow_ / 2, expireHoldsMsg_);
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

    // UDP telemetry socket — receives TELEMETRY_FRAMEs (and legacy EVENT_FRAMEs during shakedown)
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
    if(strcmp(msg->getName(), "closeTelemetryWindow") == 0)
    {
        closeTelemetryWindow();
        scheduleAt(simTime() + telemetryWindow_, msg);
    }
    else if(strcmp(msg->getName(), "expireHolds") == 0)
    {
        // Confirm departures even when no event frame has arrived to drive
        // handleEventFrame(). Only acts on UEs whose window has already elapsed —
        // this sweep controls how promptly that is noticed, never how long the
        // window is.
        expirePendingExits();
        // Rides the same tick, but only looks — see reportSilentUsers().
        reportSilentUsers();
        scheduleAt(simTime() + exitConfirmationWindow_ / 2, msg);
    }
    else
    {
        EV << "RavensControllerApp::handleSelfMessage - unknown message" << endl;
    }
}

/*
    Sends one confirmed event to the orchestrator, immediately, and tells the
    sinks about it.

    Immediately is the point. Events used to be accumulated in a map keyed by
    user and drained every two seconds, which cost up to two seconds on the
    orchestrator's most time-critical input and — because the map was keyed —
    silently discarded a user's first move if it moved twice inside one window.
    Sending as they are confirmed removes both problems at once, and there is no
    longer any container for an event to be lost in.

    The orchestrator is free to buffer these and read them at whatever decision
    step it keeps. Immediate delivery does not oblige immediate consumption; it
    only avoids this side adding delay the other side cannot recover.
*/
void RavensControllerApp::publishUserEvent(const UserEvent& event, int samplesSinceChange)
{
    for (auto& sink : telemetrySinks_)
        sink->onUserEvent(event, samplesSinceChange);

    if (!gate("outGate")->isConnected()) {
        EV << "RavensControllerApp::publishUserEvent - outGate is not connected, not sending" << endl;
        return;
    }

    inet::Packet *packet = new inet::Packet("UserEventMessage");
    auto message = inet::makeShared<UserEventMessage>();
    message->setChunkLength(controlLinkChunkLength());
    message->setType(USER_EVENT);
    message->setEvent(event);
    packet->insertAtBack(message);
    send(packet, "outGate");

    EV << "RavensControllerApp::publishUserEvent - " << userEventTypeName(event.eventType)
       << " " << event.ueAddress << " '" << event.fromMEHId << "' -> '" << event.toMEHId
       << "' (observed at " << event.observedAt << ")" << endl;
}

/*
    Forwards predictions the moment the model server answers.

    No tick, for a reason particular to this stream: a proactive migration is
    only useful if it finishes before the user arrives, so the model's horizon
    has to cover the telemetry interval, its own inference time, this hop, and
    the twelve seconds a migration takes. Every second spent waiting here is a
    second further ahead the model must have predicted. The old two-second tick
    was not just a delay but an unpredictable one — the same handover needed a
    different horizon depending on where it happened to land between ticks.
*/
void RavensControllerApp::publishPredictions(const std::vector<MigrationPrediction>& predictions)
{
    if (predictions.empty())
        return;

    if (!gate("outGate")->isConnected()) {
        EV << "RavensControllerApp::publishPredictions - outGate is not connected, not sending" << endl;
        return;
    }

    inet::Packet *packet = new inet::Packet("PredictionReportMessage");
    auto message = inet::makeShared<PredictionReportMessage>();
    message->setChunkLength(controlLinkChunkLength());
    message->setType(PREDICTION_REPORT);
    message->setPredictions(predictions);
    packet->insertAtBack(message);
    send(packet, "outGate");

    EV << "RavensControllerApp::publishPredictions - sent " << predictions.size()
       << " predictions to the orchestrator" << endl;
}

/*
    Closes the telemetry window and sends it.

    The window is one telemetryInterval long, which is not an arbitrary choice:
    each Agent sends exactly one frame per interval, so a window of that length
    holds one complete round of observations however the Agents' schedules are
    offset from one another.

    Sent even when empty. A window that nothing arrived in is a fact about the
    run, and a consumer stepping through windows needs the step to exist. The
    list of hosts that did report is what separates "host 7 had nothing to say"
    from "host 7's frame was lost" — telemetry rides UDP and is loss-tolerant by
    design, so the second really happens.
*/
void RavensControllerApp::closeTelemetryWindow()
{
    if (gate("outGate")->isConnected()) {
        inet::Packet *packet = new inet::Packet("TelemetryReportMessage");
        auto message = inet::makeShared<TelemetryReportMessage>();
        message->setChunkLength(controlLinkChunkLength());
        message->setType(TELEMETRY_REPORT);
        message->setWindowStart(windowStart_);
        message->setWindowEnd(simTime());
        message->setReportingMEHIds(windowReportingMEHs_);
        message->setUserSamples(windowUserSamples_);
        message->setCellSamples(windowCellSamples_);
        packet->insertAtBack(message);
        send(packet, "outGate");

        EV << "RavensControllerApp::closeTelemetryWindow - [" << windowStart_ << ", " << simTime()
           << "] " << windowUserSamples_.size() << " user samples, "
           << windowCellSamples_.size() << " cell samples, from "
           << windowReportingMEHs_.size() << " hosts" << endl;
    }

    windowUserSamples_.clear();
    windowCellSamples_.clear();
    windowReportingMEHs_.clear();
    windowStart_ = simTime();
}

void RavensControllerApp::socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet){
    EV << "RavensControllerApp::socketDataArrived - socket data arrived" << endl;
    inet::L3Address remoteAddress = packet->getTag<inet::L3AddressInd>()->getSrcAddress();
    int srcPort = packet->getTag<inet::L4PortInd>()->getSrcPort();

    if(ravensLinkPacketFilter.matches(packet))
    {
        EV << "RavensControllerApp::socketDataArrived(UDP) - packet received" << endl;
        auto received_packet = packet->peekAtFront<RavensLinkPacket>();
        if(received_packet->getType() == TELEMETRY_FRAME)
        {
            auto dataFrame = packet->peekAtFront<RavensLinkDataFrameMessage>();

            // Record the host as having reported before looking at what it sent:
            // a frame with neither users nor cell readings still proves the host
            // is alive and its frame arrived, which is exactly the case the
            // window's host list exists to distinguish.
            std::string reportingMEH = dataFrame->getMecHostId();
            // Which hosts reported, not how many frames arrived: jitter can land two of a
            // host's frames in one window, and this list must name each host once.
            if (std::find(windowReportingMEHs_.begin(), windowReportingMEHs_.end(), reportingMEH)
                    == windowReportingMEHs_.end())
                windowReportingMEHs_.push_back(reportingMEH);

            updateUserStateMap(dataFrame);
            // Contents first, then the frame-level hook: a sink that ships the
            // whole frame in one request needs everything in hand before it can
            // send, so onTelemetryFrame() doubles as "that was the frame".
            dispatchUserSamples(dataFrame);
            dispatchCellSamples(dataFrame);
            for (auto& sink : telemetrySinks_)
                sink->onTelemetryFrame(dataFrame);
        }
        else if(received_packet->getType() == EVENT_FRAME)
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
    // Header only: this ack carries no payload.
    request->setChunkLength(joinAckBytes());
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
    // Fixed size: three configuration values, the same on every handshake.
    request->setChunkLength(configAckBytes());
    request->setType(INFRASTRUCTURE_DETAILS_ACK);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    request->setInfoType(100);
    // Only the telemetry cadence is pushed to the Agent. exitConfirmationWindow_
    // stays here — it is the Controller's own question about the whole system,
    // and a single Agent has no way to answer it.
    request->setTelemetryIntervalMs((int)(telemetryInterval_ * 1000));
    request->setAgentMode(agentMode_);
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
    else if(received_packet->getType() == INFRASTRUCTURE_DETAILS){
        auto infraDetails = packet->peekAtFront<RavensLinkInfrastructureDetailsMessage>();
        EV << "RavensControllerApp::socketDataArrived(TCP) - INFRASTRUCTURE_DETAILS from " << infraDetails->getMecHostId() << endl;
        auto it = mehStateMap.find(infraDetails->getMecHostId());
        if(it == mehStateMap.end()){
            EV << "RavensControllerApp::socketDataArrived(TCP) - host " << infraDetails->getMecHostId() << " not found, ignoring" << endl;
            delete packet;
            return;
        }
        it->second.setAccessPoints(infraDetails->getAPList());
        sendInfrastructureDetailsAck(socket);
    }
    else if(received_packet->getType() == EVENT_FRAME){
        // Event frames arrive over the reliable TCP signaling channel (report-once
        // semantics: a lost ENTRY/EXIT would corrupt placement state permanently).
        // Periodic TELEMETRY_FRAME telemetry stays on UDP.
        auto eventMsg = packet->peekAtFront<RavensLinkEventMessage>();
        handleEventFrame(eventMsg, socket->getRemoteAddress(), socket->getRemotePort());
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
    // No-op by design: this fires while the socket is being destroyed, either from
    // socketClosed()/socketFailure() (which already removed it from socketMap on the
    // preceding line) or from deleteSockets()'s own loop, which clears socketMap right
    // after — reaching into socketMap here would erase the entry deleteSockets() is
    // currently iterating, corrupting its std::map mid-traversal.
}

void RavensControllerApp::expirePendingExits()
{
    for (auto it = userStateMap.begin(); it != userStateMap.end(); ) {
        if (it->second.pendingExitTime != 0 && simTime() >= it->second.pendingExitTime) {
            EV << "RavensControllerApp::expirePendingExits - exit hold expired for "
               << it->first << ", removing" << endl;

            UserEvent event;
            event.ueAddress  = it->first;
            event.eventType  = USER_EXIT;
            event.fromMEHId  = it->second.currentMEH;
            event.observedAt = it->second.pendingExitFirstAt;
            publishUserEvent(event, it->second.pendingExitSamples);

            it = userStateMap.erase(it);
        } else {
            ++it;
        }
    }
}

void RavensControllerApp::handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event,
                                            inet::L3Address remoteAddress, int srcPort)
{
    std::string sourceMEH = event->getMecHostId();
    EV << "RavensControllerApp::handleEventFrame - " << event->getEvents().size()
       << " events from " << sourceMEH << endl;

    // Step 1: Confirm any departures whose window has elapsed (the periodic sweep
    // is the backstop for quiet periods; this catches them as soon as a frame arrives).
    expirePendingExits();

    // Step 2: Split events by type. No confidence gate — LS is the single source of
    // truth for presence, so every detected ENTRY/EXIT is acted on. Debounce
    // (handover vs. departure) is handled entirely by the exit confirmation window
    // below, not by a per-event sample threshold. samplesSinceChange / firstDetectedAt are carried
    // through to the published event as metadata only.
    RavensEventList entries, exits;
    for (const auto& e : event->getEvents()) {
        if (e.eventType == EVENT_ENTRY) entries.push_back(e);
        else                            exits.push_back(e);
    }

    // Step 3: Apply exits — open the exit confirmation window and stash metadata fields
    for (const auto& e : exits) {
        auto userIt = userStateMap.find(e.ueAddress);
        if (userIt == userStateMap.end())
            continue;
        // Ignore a stale EXIT from a host the user has already left
        if (userIt->second.currentMEH != sourceMEH) {
            EV << "RavensControllerApp::handleEventFrame - stale EXIT for " << e.ueAddress
               << " from " << sourceMEH << " (current MEH is " << userIt->second.currentMEH
               << "), ignoring" << endl;
            continue;
        }
        userIt->second.pendingExitTime    = simTime() + exitConfirmationWindow_;
        userIt->second.pendingExitSamples = e.samplesSinceChange;
        userIt->second.pendingExitFirstAt = e.firstDetectedAt;
        EV << "RavensControllerApp::handleEventFrame - EXIT hold started for "
           << e.ueAddress << ", expires at " << userIt->second.pendingExitTime << endl;
    }

    // Step 4: Apply entries — publish at each authoritative decision point
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

            UserEvent entryEvent;
            entryEvent.ueAddress  = e.ueAddress;
            entryEvent.eventType  = USER_ENTRY;
            entryEvent.toMEHId    = sourceMEH;
            entryEvent.observedAt = e.firstDetectedAt;
            publishUserEvent(entryEvent, e.samplesSinceChange);
        } else if (userIt->second.pendingExitTime != 0) {
            // User was in exit hold — cancel it
            std::string fromMeh = userIt->second.currentMEH;
            userIt->second.pendingExitTime    = 0;
            userIt->second.pendingExitSamples = 0;
            userIt->second.timestamp          = simTime();
            if (sourceMEH != fromMeh) {
                // True handover: EXIT-from-A then ENTRY-at-B order
                userIt->second.currentMEH = sourceMEH;

                UserEvent handoverEvent;
                handoverEvent.ueAddress  = e.ueAddress;
                handoverEvent.eventType  = USER_HANDOVER;
                handoverEvent.fromMEHId  = fromMeh;
                handoverEvent.toMEHId    = sourceMEH;
                handoverEvent.observedAt = e.firstDetectedAt;
                publishUserEvent(handoverEvent, e.samplesSinceChange);
            }
            // else: re-entry at same MEH (flap) — cancel hold, nothing published
        } else {
            // No active hold
            if (sourceMEH != userIt->second.currentMEH) {
                // ENTRY from a different MEH without a prior EXIT (ENTRY-before-EXIT order)
                std::string fromMeh = userIt->second.currentMEH;
                userIt->second.currentMEH = sourceMEH;

                UserEvent handoverEvent;
                handoverEvent.ueAddress  = e.ueAddress;
                handoverEvent.eventType  = USER_HANDOVER;
                handoverEvent.fromMEHId  = fromMeh;
                handoverEvent.toMEHId    = sourceMEH;
                handoverEvent.observedAt = e.firstDetectedAt;
                publishUserEvent(handoverEvent, e.samplesSinceChange);
            }
            // else: duplicate ENTRY at same MEH — nothing published
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
    Reports users that telemetry has said nothing about for implausibly long.
    It removes nothing and tells no one — it looks.

    Users leave userStateMap through one path only: expirePendingExits(), after an
    EXIT event and its confirmation window. Placement belongs to the event channel,
    and a clock is not the event channel. This function used to delete the user and
    report a departure, which gave telemetry a power it is explicitly denied
    elsewhere — updateUserStateMap() refuses to let a frame introduce a user or move
    one, yet a run of missed frames could remove one, and the departure it reported
    tore down that user's application while the user was still there.

    What it measures is worth keeping. Events ride TCP and the Agent retries them
    while the channel is down, so a placement change should never go missing. A run
    reporting no silent users is evidence of exactly that; a run reporting some has
    a bug worth finding. Hence the scalar in finish() rather than a log line alone.

    Reported once per user. Silence is a state, not an event, and re-reporting it
    every sweep would turn one problem into forty log lines.

    Only meaningful where telemetry is flowing. With telemetry off nothing is sent
    about a user that is simply staying put — that is what report-once means — so
    silence is the normal steady state there and every stationary user would be
    reported.
*/
void RavensControllerApp::reportSilentUsers(){
    if (!collectTelemetry_)
        return;

    simtime_t actual = simTime();
    for (auto& [address, user] : userStateMap) {
        if (user.silenceReported || actual - user.timestamp <= silenceWarningThreshold_)
            continue;

        user.silenceReported = true;
        silentUsersReported_++;
        EV_WARN << "RavensControllerApp::reportSilentUsers - no telemetry about " << address
                << " for " << (actual - user.timestamp) << "s, still held at "
                << user.currentMEH << " with no EXIT event" << endl;
    }
}

/*
    Marks every user named in a telemetry frame as still alive. That is the
    whole job.

    Placement is decided exclusively by the event channel. A telemetry frame
    never moves a user between hosts and never introduces one: presence has to
    be confirmed by an entry event first, and acting on telemetry here would
    pre-empt that confirmation, so the entry event would never be published for
    that user.

    The timestamp being refreshed feeds the silence
    diagnostic in reportSilentUsers(), which warns and counts but never acts.
    Without the refresh, a user who entered and then stayed on the same host
    would be reported as silent despite being perfectly alive. That used to be
    worse than a false warning: the same timestamp drove a timeout that deleted
    the user and reported a departure, and in the profile where no telemetry
    refreshes it, every stationary user was retired about a minute after
    arriving and had its application torn down underneath it.

    A user appears at most once per frame, so there is no question of which
    observation "wins": what is recorded is the frame's own timestamp, not the
    observation's.
*/
void RavensControllerApp::updateUserStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) {
    for (const auto& observation : received_packet->getUserSamples()) {
        auto userIt = userStateMap.find(observation.getAddress());
        if (userIt == userStateMap.end()) {
            // Not yet confirmed by an entry event — skip, as above.
            EV << "RavensControllerApp::updateUserStateMap - TELEMETRY_FRAME for unknown user "
               << observation.getAddress() << ", skipping (waiting for ENTRY event)" << endl;
            continue;
        }
        if (received_packet->getTimeStamp() < userIt->second.timestamp) {
            EV << "RavensControllerApp::updateUserStateMap - Ignored stale update for user "
               << observation.getAddress() << endl;
            continue;
        }
        userIt->second.timestamp = received_packet->getTimeStamp();
    }
}

/*
    Hands the frame's observations to the sinks, one user at a time, and adds
    them to the open telemetry window.

    Every observation becomes a UserSample here and nowhere else. That is the
    whole point of this function: the CSV columns, the prediction payload and now
    the orchestrator's telemetry are written by different consumers, in different
    runs, months apart, and nothing at runtime could ever reveal a disagreement
    between them. Producing the record once removes the possibility instead of
    relying on all of them being kept in step by hand.

    Nothing is held back or reordered. The observations go out in the order the
    Agent put them in the frame, and the frame is finished when this returns. Any
    windowing a sequence model needs belongs to whoever consumes the samples.

    The sink hook takes a sequence, and a frame supplies exactly one observation
    per user, so every call carries a single element. The shape is kept because
    it is the shape a consumer wants — a per-user sequence — and because the
    frame interval is a parameter: this is one point on it, not a property of the
    design.

    confirmedMEH is read per user from the Controller's current belief. It is
    empty when the user is not in userStateMap at all: an ENTRY takes several
    consecutive Location Service samples to confirm, so every user spends its
    first seconds observed but not yet confirmed, and its telemetry legitimately
    arrives with nothing to compare against.
*/
void RavensControllerApp::dispatchUserSamples(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) {
    for (const auto& observation : received_packet->getUserSamples()) {
        std::string address = observation.getAddress();
        auto userIt = userStateMap.find(address);
        // Did we find this user? If yes, take the host we believe they're on. If no, use an empty string
        std::string confirmedMEH = (userIt != userStateMap.end()) ? userIt->second.currentMEH : "";

        std::vector<UserSample> samples;
        samples.push_back(makeUserSample(received_packet, address, observation, confirmedMEH));

        for (auto& sink : telemetrySinks_)
            sink->onUserSamples(samples);

        windowUserSamples_.push_back(samples.front());
    }
}

/*
    Hands the frame's cell readings to the sinks in one call, and adds them to
    the open window.

    A sequence rather than a single reading, because a frame carries one per cell
    or none: nothing is sent for a cell until the RNIS has first replied, and an
    empty frame section says that plainly.

    The Controller does not read these at all — nothing on this side consumes
    cell metrics. They pass through so every consumer receives them in the same
    canonical form the CSV and the payload are both written from.
*/
void RavensControllerApp::dispatchCellSamples(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) {
    const auto& readings = received_packet->getCellSamples();
    if (readings.empty())
        return;

    std::vector<CellSample> samples;
    samples.reserve(readings.size());
    for (const auto& reading : readings)
        samples.push_back(makeCellSample(received_packet, reading));

    for (auto& sink : telemetrySinks_)
        sink->onCellSamples(samples);

    windowCellSamples_.insert(windowCellSamples_.end(), samples.begin(), samples.end());
}


} // namespace
