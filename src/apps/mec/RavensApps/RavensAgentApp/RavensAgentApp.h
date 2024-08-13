

#ifndef _APPS_MEC_MEAPPS_RAAPP_H_
#define _APPS_MEC_MEAPPS_RAAPP_H_

#define JOIN_NETWORK_REQUEST 0
#define JOIN_NETWORK_ACK 1
#define INFRAESTRUCTURE_DETAILS 2
#define INFRAESTRUCTURE_DETAILS_ACK 3
#define SET_RETRIEVAL_INTERVAL 4
#define SET_RETRIEVAL_INTERVAL_ACK 5
#define USERS_INFO_SNAPSHOT 6

#include "omnetpp.h"

#include "inet/networklayer/common/L3Address.h"
#include "inet/networklayer/common/L3AddressResolver.h"

#include "nodes/mec/MECPlatform/ServiceRegistry/ServiceRegistry.h"

#include "apps/mec/MecApps/MecAppBase.h"
#include "inet/common/lifecycle/NodeStatus.h"

#include "AccessPointData.h"
#include "UserData.h"
#include "UserLocation.h"
#include "NodeLocation.h"

#include "../RavensLinkPacket_m.h"

namespace simu5g {

using namespace std;

using namespace omnetpp;

class RavensAgentApp : public MecAppBase, public inet::UdpSocket::ICallback  
{
protected:
    simtime_t sendInterval;
    int localSnapshotCounter;

    std::string mecHostId;

    inet::UdpSocket controllerSocket_;
    int localPort_;

    int controllerPort;
    inet::L3Address controllerAddress_;

    inet::TcpSocket* lsSocket_;
    inet::TcpSocket* mp1Socket_;

    HttpBaseMessage* mp1HttpMessage;
    HttpBaseMessage* serviceHttpMessage;

    cMessage *userList;
    cMessage *userLocation;
    
    std::vector<AccessPointData> accessPoints;
    std::map<std::string, UserData> users; 
    std::map<long, std::map<std::string, UserData>> history;

    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void finish() override;

    virtual void handleProcessedMessage(omnetpp::cMessage *msg) override;

    virtual void handleHttpMessage(int connId) override;
    virtual void handleServiceMessage(int connId) override;
    virtual void handleMp1Message(int connId) override;
    virtual void handleUeMessage(omnetpp::cMessage *msg) override;

    virtual void handleSelfMessage(cMessage *msg) override;

    virtual void established(int connId) override;

    void handleLSMessage(int connId);

    void sendUserListRequest();
    void sendUserLocationRequest();
    void sendAPListRequest();

    void sendUsersDensitySubscription();
    void sendUsersListSubscription();

    void connectToRavensController();
    void sendJoinNetworkRequest();
    void sendAPList();
    void sendUsersInfoSnapshot();

    simtime_t getRetrievalInterval();
    void setRetrievalInterval(simtime_t interval);

    // udp socket callback methods
    virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override;
    virtual void socketClosed(inet::UdpSocket *socket) override;

    std::string collectionString(std::vector<std::string> vec);


public:
    RavensAgentApp();
    virtual ~RavensAgentApp();

    std::string getMecHostId();
};

}

#endif
