#ifndef _RAVENS_CONTROLLER_APP_H
#define _RAVENS_CONTROLLER_APP_H

#define JOIN_NETWORK_REQUEST 0
#define JOIN_NETWORK_ACK 1
#define INFRAESTRUCTURE_DETAILS 2
#define INFRAESTRUCTURE_DETAILS_ACK 3
#define SET_RETRIEVAL_INTERVAL 4
#define SET_RETRIEVAL_INTERVAL_ACK 5
#define USERS_INFO_SNAPSHOT 6

#define CHANGE_ENTRY 0
#define CHANGE_MEH 1
#define CHANGE_POSITION 2
#define CHANGE_EXIT 3
#define NO_CHANGE 4

#include <inet/networklayer/common/L3AddressResolver.h>
#include <inet/transportlayer/contract/udp/UdpSocket.h>
#include <inet/common/socket/SocketMap.h>
#include <inet/applications/base/ApplicationBase.h>
#include <inet/common/packet/PacketFilter.h>

#include "MECHostData.h"
#include "DataUpdates/UserMEHUpdate.h"
#include "DataUpdates/UserEntryUpdate.h"

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
};

// structure that contains the type of change and the user data at the moment the change happens
struct UserStateChange
{
    int changeType;
    UserData userData;
};

class LocationDataHandlerPolicyBase;

class RavensControllerApp: public inet::ApplicationBase, public inet::UdpSocket::ICallback
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

        // Data structures to be sent to the MEO depending on the mode we are in
        std::vector<UserMEHUpdate> userUpdates; 
        std::vector<UserEntryUpdate> userEntryUpdates; 

        inet::UdpSocket udpSocket;
        inet::SocketMap socketMap;

        friend class LocationDataHandlerPolicyBase;
        friend class SaveDataHistory;
        friend class NotifyOnDataChange;
        friend class NotifyOnUserEntry;
        
        LocationDataHandlerPolicyBase* locationDataHandlerPolicy_;

        // to check if we are dealing with a packet from RAVENS Agent or from a UE directly
        inet::PacketFilter ravensLinkPacketFilter;
        inet::PacketFilter uePacketFilter;

        cMessage *calculateAvg_;

    protected:
        virtual void initialize(int stage) override;

        virtual void handleMessageWhenUp(cMessage *msg) override;
        virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
        virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
        virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

        // UdpSocket::ICallback mandatory methods
        virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
        virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override;
        virtual void socketClosed(inet::UdpSocket *socket) override;
    
        void sendJoinNetworkAck(inet::UdpSocket *socket, inet::L3Address remoteAddress, int port);
        void sendInfrastructureDetailsAck(inet::UdpSocket *socket, inet::L3Address remoteAddress, int port);

        // methods to deal with mehStateMap and userStateMap
        // std::vector<std::pair<std::string, std::string>> detectInactiveUsers();

        // methods to deal with userStateMap
        void updateUserStateMap(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet);
        std::vector<UserState> removeInactiveUsers();

        std::string getMecHostIdFromAccessPointId(std::string accessPointId);

        void handleSelfMessage(inet::cMessage *msg);

        void calculateAvgNetworkData();
    
    public:
        RavensControllerApp();
        ~RavensControllerApp();
    
};

}

#endif
