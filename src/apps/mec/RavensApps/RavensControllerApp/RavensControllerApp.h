#ifndef _RAVENS_CONTROLLER_APP_H
#define _RAVENS_CONTROLLER_APP_H

// Message types (Agent <-> Controller)
#define JOIN_NETWORK_REQUEST    0
#define JOIN_NETWORK_ACK        1
#define INFRAESTRUCTURE_DETAILS     2
#define INFRAESTRUCTURE_DETAILS_ACK 3
#define DATA_FRAME              6
#define UE_EVENT                8  // fresh value — avoids the USERS_UPDATE=7 collision (F3)

// Agent operating mode (sent in INFRAESTRUCTURE_DETAILS_ACK)
#define AGENT_MODE_EVENT_ONLY       0
#define AGENT_MODE_EVENT_AND_DATA   1

// Event subtypes (payload of UE_EVENT)
#define EVENT_ENTRY 0
#define EVENT_EXIT  1

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

// the most updated state of a given user
struct UserState
{
    std::string userId;
    std::string currentMEH;
    simtime_t timestamp;
    UserData userData;
    std::string pendingMEH;       // MEH attempting handover (empty if none)
    simtime_t pendingExitTime;    // non-zero when F2 exit-hold window is active
    int pendingExitSamples = 0;   // samplesSinceChange of the EXIT that opened the hold
    simtime_t pendingExitFirstAt; // firstDetectedAt of that EXIT
};

// structure that contains the type of change and the user data at the moment the change happens
struct UserStateChange
{
    int changeType;
    UserData userData;
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

        int threshold_;

        int confirmationCount_;         // min samplesSinceChange on ENTRY to confirm handover
        int exitConfidenceThreshold_;   // min samplesSinceChange on EXIT to act on it
        double frameInterval_;          // pushed to Agents in INFRAESTRUCTURE_DETAILS_ACK

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
        std::vector<UserState> removeInactiveUsers();

        // Handles UE_EVENT packets (ENTRY/EXIT deltas from Agent)
        void handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event,
                              inet::L3Address remoteAddress, int srcPort);

        std::string getMecHostIdFromAccessPointId(std::string accessPointId);

        void handleSelfMessage(inet::cMessage *msg);

    public:
        RavensControllerApp();
        ~RavensControllerApp();
    
};

}

#endif
