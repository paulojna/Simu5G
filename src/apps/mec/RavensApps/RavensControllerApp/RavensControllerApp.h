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

class LocationDataHandlerPolicyBase;

class RavensControllerApp: public inet::ApplicationBase,
                           public inet::UdpSocket::ICallback,
                           public inet::TcpSocket::ICallback
{
    private:
        // Structures to hold the state of the MEHs and the users and identify changes in the data
        std::unordered_map<std::string, MECHostData> mehStateMap;
        std::unordered_map<std::string, UserState> userStateMap;

        // update to be sent to the MEO
        inet::Packet *update;

        // When to start sending the snapshots to the MEO and at which frequency
        int snapshot_frequency_;
        int snapshot_starting_time_;

        int silenceWarningThreshold_;     // how long telemetry may say nothing about a
                                          // user before that silence is reported (s)
        int agentMode_;                   // EVENT_ONLY_MODE or FULL_MODE, decided by the
                                          // configured output policy and pushed to Agents
        long silentUsersReported_ = 0;    // users seen going implausibly silent, recorded
                                          // as a scalar: it should be zero

        double telemetryInterval_;        // pushed to Agents in INFRASTRUCTURE_DETAILS_ACK
        double exitConfirmationWindow_;   // Controller-private; how long to wait before
                                          // treating a reported EXIT as leaving the system

        // Data structures to be sent to the MEO depending on the mode we are in
        // PERFORMANCE IMPROVEMENT: Changed from vector to map for O(1) lookup in addUserUpdate()
        // Original: std::vector<UserMEHUpdate> userUpdates;
        std::unordered_map<std::string, UserMEHUpdate> userUpdates;
		std::unordered_map<std::string, MigrationPrediction> migrationPredictions;

        inet::UdpSocket udpSocket;
        inet::TcpSocket serverSocket_;  // TCP listener on mgmtPort — accepts Agent handshake connections
        inet::SocketMap socketMap;

        friend class LocationDataHandlerPolicyBase;
        friend class SaveDataHistory;
        friend class NotifyOnDataChange;
        friend class SendToExternalServer;
        
        LocationDataHandlerPolicyBase* locationDataHandlerPolicy_;

        // to check if we are dealing with a packet from RAVENS Agent or from a UE directly
        inet::PacketFilter ravensLinkPacketFilter;
        inet::PacketFilter uePacketFilter;

        cMessage *calculateAvg_;
        cMessage *expireHoldsMsg_;   // periodic sweep for elapsed exit confirmation windows

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

        // methods to deal with mehStateMap and userStateMap
        // std::vector<std::pair<std::string, std::string>> detectInactiveUsers();

        // methods to deal with userStateMap
        void updateUserStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);
        void updateMehStateMap(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);

        // Turns every observation in a telemetry frame into the canonical sample
        // record and hands it to the output policy, one UE at a time.
        void dispatchUserSamples(inet::Ptr<const RavensLinkDataFrameMessage> received_packet);

        // Warns about users telemetry has said nothing about for implausibly long,
        // and counts them. Removes nothing: users leave userStateMap only through
        // the event channel, via expirePendingExits().
        void reportSilentUsers();

        // Handles EVENT_FRAME packets (ENTRY/EXIT deltas from Agent)
        void handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event,
                              inet::L3Address remoteAddress, int srcPort);

        // Sweeps userStateMap for elapsed exit confirmation windows, emits onUserExit, and removes
        // the user. Called both inline from handleEventFrame (prompt path) and from a
        // periodic self-message (liveness when no event frames are arriving).
        void expirePendingExits();

        std::string getMecHostIdFromAccessPointId(std::string accessPointId);

        void handleSelfMessage(inet::cMessage *msg);

    public:
        RavensControllerApp();
        ~RavensControllerApp();
    
};

}

#endif
