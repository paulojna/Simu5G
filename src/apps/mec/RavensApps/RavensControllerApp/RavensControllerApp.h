#ifndef _RAVENS_CONTROLLER_APP_H
#define _RAVENS_CONTROLLER_APP_H

// RavensLink frame types, agent modes and event subtypes (shared with the Agent)
#include "apps/mec/RavensApps/RavensLinkProtocol.h"
// Stream types and user event types (shared with the MEC orchestrator)
#include "apps/mec/RavensApps/RavensControlProtocol.h"

#include <inet/networklayer/common/L3AddressResolver.h>
#include <inet/transportlayer/contract/udp/UdpSocket.h>
#include <inet/transportlayer/contract/tcp/TcpSocket.h>
#include <inet/common/socket/SocketMap.h>
#include <inet/applications/base/ApplicationBase.h>
#include <inet/common/packet/PacketFilter.h>

#include "MECHostData.h"
#include "DataUpdates/UserEvent.h"
#include "DataUpdates/MigrationPrediction.h"
#include "DataUpdates/TelemetrySample.h"

#include "../RavensLinkPacket_m.h"
#include "../RavensControlPacket_m.h"
#include "../UsersInfoPacket_m.h"
#include "../RavensAgentApp/UserData.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <map>
#include <memory>

namespace simu5g {
	using namespace omnetpp;

// What the Controller believes about one user: which host it is on, and how far
// along any departure from that host has got.
//
// Note what is *not* here: the user's telemetry. The Controller used to keep a
// copy of each user's latest position and radio values, and nothing ever read
// it — both outputs take telemetry straight from the arriving frame. Keeping it
// would also have forced an arbitrary choice now that a frame carries several
// observations per user: which one becomes "the" stored copy? Removing the
// field removes the question.
//
// The timestamp stays, and is not telemetry: it is a liveness mark, the only
// input to the silence diagnostic in reportSilentUsers(). Nothing acts on it —
// a user is retired by the event channel alone.
struct UserState
{
    std::string userId;
    std::string currentMEH;
    simtime_t timestamp;          // last time anything was heard about this user
    simtime_t pendingExitTime;    // non-zero while the exit confirmation window is open
    int pendingExitSamples = 0;   // samplesSinceChange of the EXIT that opened the window
    simtime_t pendingExitFirstAt; // firstDetectedAt of that EXIT
    bool silenceReported = false; // already warned about implausibly long silence
};

class TelemetrySink;

class RavensControllerApp: public inet::ApplicationBase,
                           public inet::UdpSocket::ICallback,
                           public inet::TcpSocket::ICallback
{
    private:
        // Structures to hold the state of the MEHs and the users and identify changes in the data
        std::unordered_map<std::string, MECHostData> mehStateMap;
        std::unordered_map<std::string, UserState> userStateMap;

        int silenceWarningThreshold_;     // how long telemetry may say nothing about a
                                          // user before that silence is reported (s)
        int agentMode_;                   // EVENT_ONLY_MODE or FULL_MODE, decided by whether
                                          // this run collects telemetry, and pushed to Agents
        long silentUsersReported_ = 0;    // users seen going implausibly silent, recorded
                                          // as a scalar: it should be zero

        double telemetryInterval_;        // pushed to Agents in INFRASTRUCTURE_DETAILS_ACK
        double exitConfirmationWindow_;   // Controller-private; how long to wait before
                                          // treating a reported EXIT as leaving the system

        bool collectTelemetry_;           // whether Agents are asked for telemetry at all;
                                          // derived in initialize() from who consumes it,
                                          // never set directly by configuration
        bool callModelServer_ = false;    // derived alongside it: true exactly when the
                                          // orchestrator's strategy consumes predictions
        double telemetryWindow_;          // how much telemetry goes into one report (s)

        // The window currently being filled, drained whole when it closes. This
        // is the only outbound buffer left: user events and predictions are sent
        // the moment they exist. Appending, never keyed — the map this replaced
        // was keyed by user, so a user that moved twice inside one window lost
        // its first move.
        std::vector<UserSample> windowUserSamples_;
        std::vector<CellSample> windowCellSamples_;
        std::vector<std::string> windowReportingMEHs_;
        simtime_t windowStart_;

        inet::UdpSocket udpSocket;
        inet::TcpSocket serverSocket_;  // TCP listener on mgmtPort — accepts Agent handshake connections
        inet::SocketMap socketMap;

        // Everything that wants a copy of the observations, besides the
        // orchestrator. A list, not a choice: recording a run to file and driving
        // a model from it are unrelated, and both can be on.
        std::vector<std::unique_ptr<TelemetrySink>> telemetrySinks_;

        // to check if we are dealing with a packet from RAVENS Agent or from a UE directly
        inet::PacketFilter ravensLinkPacketFilter;
        inet::PacketFilter uePacketFilter;

        cMessage *closeTelemetryWindowMsg_; // periodic close of the telemetry window
        cMessage *expireHoldsMsg_;          // periodic sweep for elapsed exit confirmation windows

        // Predictions waiting out the model's inference time, keyed by when they
        // are due. See deliverPredictions() for why they wait at all.
        //
        // A multimap rather than one pending batch: inferenceTime is volatile, so
        // it may be drawn from a distribution, and a short draw can legitimately
        // overtake a long one. Keyed by due time, the earliest is always the next
        // one out however they were scheduled.
        std::multimap<simtime_t, std::vector<MigrationPrediction>> pendingPredictions_;

        // One timer for all of them, always set to the earliest due time, rather
        // than one message per batch. Keeps the teardown the same as every other
        // timer here — a single cancelAndDelete — instead of a set of in-flight
        // messages to chase.
        cMessage *deliverPredictionsMsg_;

    protected:
        virtual void initialize(int stage) override;
        virtual void finish() override;

        virtual void handleMessageWhenUp(cMessage *msg) override;
        virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
        virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
        virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

        // UdpSocket::ICallback
        virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
        virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override;
        virtual void socketClosed(inet::UdpSocket *socket) override;

        // TcpSocket::ICallback
        virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *availableInfo) override;
        virtual void socketEstablished(inet::TcpSocket *socket) override;
        virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *msg, bool urgent) override;
        virtual void socketPeerClosed(inet::TcpSocket *socket) override;
        virtual void socketClosed(inet::TcpSocket *socket) override;
        virtual void socketFailure(inet::TcpSocket *socket, int code) override;
        virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override;
        virtual void socketDeleted(inet::TcpSocket *socket) override;

        void sendJoinNetworkAck(inet::TcpSocket *socket);
        void sendInfrastructureDetailsAck(inet::TcpSocket *socket);

        // Sends one confirmed event to the orchestrator, and tells the sinks
        // about it. Called the moment the event is concluded — there is no
        // queue and no tick between deciding and reporting.
        void publishUserEvent(const UserEvent& event, int samplesSinceChange);

        // Closes the current telemetry window and sends it. Scheduled every
        // telemetryWindow_ seconds; see the parameter for why that length.
        void closeTelemetryWindow();

        // methods to deal with userStateMap
        void updateUserStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);

        // Turns every observation in a telemetry frame into the canonical sample
        // record, hands it to the sinks, and adds it to the open window.
        void dispatchUserSamples(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);

        // The same for the frame's cell readings, handed over in one call.
        void dispatchCellSamples(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);

        // Warns about users telemetry has said nothing about for implausibly long,
        // and counts them. Removes nothing: users leave userStateMap only through
        // the event channel, via expirePendingExits().
        void reportSilentUsers();

        // Handles EVENT_FRAME packets (ENTRY/EXIT deltas from Agent)
        void handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event,
                              inet::L3Address remoteAddress, int srcPort);

        // Sweeps userStateMap for elapsed exit confirmation windows, reports the departure,
        // and removes the user. Called both inline from handleEventFrame (prompt path) and
        // from a periodic self-message (liveness when no event frames are arriving).
        void expirePendingExits();

        void handleSelfMessage(inet::cMessage *msg);

    public:
        RavensControllerApp();
        ~RavensControllerApp();

        // Takes predictions from the model server and hands them to the
        // orchestrator once the model's inference time has elapsed. Public
        // because PredictionServerClient calls it — the sinks used to reach into
        // a member map through a friend declaration, which meant four classes
        // could write the Controller's outbound state.
        //
        // A sink cannot do the waiting itself: scheduling belongs to a module,
        // and a sink is not one.
        void deliverPredictions(const std::vector<MigrationPrediction>& predictions);

        // Sends them on. Called by deliverPredictions() when the wait is over.
        void publishPredictions(const std::vector<MigrationPrediction>& predictions);
};

}

#endif
