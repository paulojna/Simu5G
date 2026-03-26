

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

#include "AccessPointRadioInfoData.h"

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

	// to work with our Lazy Heartbeat logic
	simtime_t forceUpdateInterval_;
	simtime_t lastSentTimestamp_;
    simtime_t ttl_; // Added TTL for user data freshness
    bool hasPendingUpdates_; // Dirty flag to avoid full user scan

    std::string mecHostId;

    inet::UdpSocket controllerSocket_;
    int localPort_;

    int controllerPort;
    inet::L3Address controllerAddress_;

    // RAVENS V3 - Using RNIS besides LS
    int rnisPort;
    inet::L3Address rnisAddress;

    inet::TcpSocket* lsSocket_;
    inet::TcpSocket* mp1Socket_;
    inet::TcpSocket* rnisSocket_;

    HttpBaseMessage* mp1HttpMessage;
    HttpBaseMessage* serviceHttpMessage;

    cMessage *userList;


    std::vector<AccessPointData> accessPoints;
	std::unordered_map<std::string, AccessPointData*> apIndex_;
    std::unordered_map<std::string, UserData> users;

	AccessPointRadioInfoData* accessPointRadioInformation;

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
	void handleRNISMessage(int connId);

    void sendUserListRequest();
	// for RAVENS V3
    void sendUserLocationRequest();
	void sendRNISRequest();
    void sendAPListRequest();

    void sendUsersDensitySubscription();
    void sendUsersListSubscription();
	void sendL2MeasSubscription();

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
	virtual void socketClosed(inet::TcpSocket *socket) override;


public:
    RavensAgentApp();
    virtual ~RavensAgentApp();

    std::string getMecHostId();
};

}

#endif
