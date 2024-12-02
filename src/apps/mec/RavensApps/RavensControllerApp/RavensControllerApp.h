#ifndef _RAVENS_CONTROLLER_APP_H
#define _RAVENS_CONTROLLER_APP_H

#define JOIN_NETWORK_REQUEST 0
#define JOIN_NETWORK_ACK 1
#define INFRAESTRUCTURE_DETAILS 2
#define INFRAESTRUCTURE_DETAILS_ACK 3
#define SET_RETRIEVAL_INTERVAL 4
#define SET_RETRIEVAL_INTERVAL_ACK 5
#define USERS_INFO_SNAPSHOT 6

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

#include <iostream>
#include <fstream>

namespace simu5g {

using namespace omnetpp;

struct mecHostNetworkData
{
    std::string mecHostId;
    simtime_t lastUpdate;
    double lastAvgRTT;
    double lastAvgLostPackets;
    std::list<double> avgRTT;
    std::list<int> avgLostPackets;
};

class DataHandlerPolicyBase;

class RavensControllerApp: public inet::ApplicationBase, public inet::UdpSocket::ICallback
{
    private:
        std::map<std::string, MECHostData> hostsData;
        std::map<simtime_t, std::map<std::string, MECHostData>> hostsDataHistory;
        std::map<std::string, mecHostNetworkData> hostsNetworkData;

        int bufferTime_;
        int snapshot_frequency_;
        int snapshot_starting_time_;
        std::vector<UserMEHUpdate> userUpdates; 
        std::vector<UserEntryUpdate> userEntryUpdates; 
        simtime_t rcUpdateInterval; // start time of the simulation

        inet::UdpSocket udpSocket;
        inet::SocketMap socketMap;

        friend class DataHandlerPolicyBase;
        friend class SaveDataHistory;
        friend class NotifyOnDataChange;
        
        DataHandlerPolicyBase* dataHandlerPolicy_;

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

        void handleSelfMessage(inet::cMessage *msg);

        void calculateAvgNetworkData();
    
    public:
        RavensControllerApp();
        ~RavensControllerApp();
    
};

}

#endif
