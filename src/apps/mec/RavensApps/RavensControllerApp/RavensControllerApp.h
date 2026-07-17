#ifndef _RAVENS_CONTROLLER_APP_H
#define _RAVENS_CONTROLLER_APP_H

// RavensLink frame types, agent modes and event subtypes (shared with the Agent)
#include "apps/mec/RavensApps/RavensLinkProtocol.h"

#include <inet/networklayer/common/L3AddressResolver.h>
#include <inet/transportlayer/contract/udp/UdpSocket.h>
#include <inet/transportlayer/contract/tcp/TcpSocket.h>
#include <inet/common/socket/SocketMap.h>
#include <inet/applications/base/ApplicationBase.h>
#include <inet/common/packet/PacketFilter.h>
#include <inet/common/packet/ChunkQueue.h>

#include "MECHostData.h"
#include "DataUpdates/UserMEHUpdate.h"
#include "DataUpdates/MigrationPrediction.h"

#include "../RavensLinkPacket_m.h"
#include "../UsersInfoPacket_m.h"
#include "../RavensAgentApp/UserData.h"

#include <iostream>
#include <fstream>

namespace simu5g {
	using namespace omnetpp;

// the most updated state of a given user
struct UserState
{
    std::string userId;
    std::string currentMEH;
    simtime_t timestamp;
    UserData userData;
    simtime_t pendingExitTime;    // non-zero when F2 exit-hold window is active
    int pendingExitSamples = 0;   // samplesSinceChange of the EXIT that opened the hold
    simtime_t pendingExitFirstAt; // firstDetectedAt of that EXIT
    bool staleWarned = false;     // stale-telemetry warning already emitted for this user
};

class RavensOutputBase;

class RavensControllerApp: public inet::ApplicationBase,
                           public inet::UdpSocket::ICallback,
                           public inet::TcpSocket::ICallback
{
    private:
        // Structures to hold the state of the MEHs and the users and identify changes in the data
        std::unordered_map<std::string, MECHostData> mehStateMap;
        std::unordered_map<std::string, UserState> userStateMap;

        // When to start sending the snapshots to the MEO and at which frequency
        int snapshot_frequency_;
        int snapshot_starting_time_;

        int staleWarningThreshold_;     // telemetry silence (s) after which a user is
                                        // flagged as stale (warning only, FULL mode only)

        double frameInterval_;          // pushed to Agents in INFRASTRUCTURE_DETAILS_ACK;
                                        // also the F2 exit-hold window length

        std::string profile_;           // "History", "Prediction" or "Reaction"
        int agentMode_;                 // mode sent to Agents, derived from profile_
        std::string runDir_;            // <path>/<profile>/run_<N>/ — created by the
                                        // Controller, shared by all file-writing outputs
                                        // (empty in the Reaction profile)

        // Data structures to be sent to the MEO depending on the mode we are in
        // PERFORMANCE IMPROVEMENT: Changed from vector to map for O(1) lookup in addUserUpdate()
        // Original: std::vector<UserMEHUpdate> userUpdates;
        std::unordered_map<std::string, UserMEHUpdate> userUpdates;
		std::unordered_map<std::string, MigrationPrediction> migrationPredictions;

        inet::UdpSocket udpSocket;
        inet::TcpSocket serverSocket_;  // TCP listener on mgmtPort — accepts Agent handshake connections
        inet::SocketMap socketMap;

        // Per-connection TCP reassembly queues (keyed by socketId). TCP is a byte
        // stream: one delivery may carry several RavensLink messages (e.g. burst
        // after a retransmission) or a partial one — messages are popped only
        // when complete.
        std::map<int, inet::ChunkQueue> socketQueues_;

        friend class RavensOutputBase;
        friend class HistoryOutput;
        friend class MeoOutput;
        friend class PredictionOutput;

        // Outputs selected by profile_; the core publishes telemetry and lifecycle
        // hooks to every registered output (see RavensOutputBase).
        std::vector<RavensOutputBase*> outputs_;

        // to check if we are dealing with a packet from RAVENS Agent or from a UE directly
        inet::PacketFilter ravensLinkPacketFilter;

        cMessage *sendSnapshotMsg_;  // periodic MEO snapshot flush
        cMessage *expireHoldsMsg_;   // periodic F2 hold-expiry sweep (frame-independent liveness)

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

        // methods to deal with userStateMap
        void updateUserStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);
        void updateMehStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);

        // Diagnostic only (FULL mode): warns once per user whose telemetry went
        // silent for longer than staleWarningThreshold_. Departures always arrive
        // as reliable EXIT events, so staleness can only mean a pipeline bug —
        // it is reported loudly but never mutates state.
        void warnStaleUsers();

        // Handles EVENT_FRAME messages (ENTRY/EXIT deltas from Agent, TCP signaling channel)
        void handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event);

        // Sweeps userStateMap for elapsed F2 exit-holds, emits onUserExit, and removes
        // the user. Called both inline from handleEventFrame (prompt path) and from a
        // periodic self-message (liveness when no event frames are arriving).
        void expirePendingExits();

        void handleSelfMessage(inet::cMessage *msg);

    public:
        RavensControllerApp();
        ~RavensControllerApp();
    
};

}

#endif
