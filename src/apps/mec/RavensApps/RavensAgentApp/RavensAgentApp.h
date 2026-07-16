

#ifndef _APPS_MEC_MEAPPS_RAAPP_H_
#define _APPS_MEC_MEAPPS_RAAPP_H_

#include <cstddef>
#include <iterator>

// RavensLink frame types, agent modes and event subtypes (shared with the Controller)
#include "apps/mec/RavensApps/RavensLinkProtocol.h"

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
    int localSnapshotCounter;

    simtime_t frameInterval_;   // negotiated with Controller, used for both frame types
    int agentMode_;             // EVENT_ONLY_MODE, TELEMETRY_ONLY_MODE, or FULL_MODE

    // Pending events — accumulated between frame sends, cleared after each frame
    struct PendingEvent {
        simtime_t firstDetectedAt;
        int       sampleCount;
    };
    std::unordered_map<std::string, PendingEvent> pendingEntries_; // users appeared since last frame
    std::unordered_map<std::string, PendingEvent> pendingExits_;   // users absent since last frame

    std::string mecHostId;

    inet::UdpSocket controllerSocket_;      // UDP — event frames + data frames to Controller dataPort
    inet::TcpSocket controllerMgmtSocket_; // TCP — config handshake to Controller mgmtPort (close-after-ACK)
    int localPort_;

    int controllerPort;
    int controllerMgmtPort_;
    inet::L3Address controllerAddress_;

    // RAVENS V3 - Using RNIS besides LS
    int rnisPort;
    inet::L3Address rnisAddress;

    inet::TcpSocket* lsSocket_;
    inet::TcpSocket* mp1Socket_;
    inet::TcpSocket* rnisSocket_;

    HttpBaseMessage* mp1HttpMessage;
    HttpBaseMessage* serviceHttpMessage;

    std::vector<AccessPointData> accessPoints;
	std::unordered_map<std::string, size_t> apIndex_;
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
    void sendEventFrame();     // sends event frame if pending entries/exits exist (UDP)
    void sendDataFrame();      // sends telemetry frame only if agentMode_ == FULL_MODE (UDP)

    // UdpSocket::ICallback
    virtual void socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet) override;
    virtual void socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication) override;
    virtual void socketClosed(inet::UdpSocket *socket) override;

    // TcpSocket::ICallback — for controllerMgmtSocket_ (config handshake)
    virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *availableInfo) override;
    virtual void socketEstablished(inet::TcpSocket *socket) override;
    virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *msg, bool urgent) override;
    virtual void socketPeerClosed(inet::TcpSocket *socket) override;
    virtual void socketClosed(inet::TcpSocket *socket) override;
    virtual void socketFailure(inet::TcpSocket *socket, int code) override;
    virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override;
    virtual void socketDeleted(inet::TcpSocket *socket) override;


public:
    RavensAgentApp();
    virtual ~RavensAgentApp();

    std::string getMecHostId();
};

}

#endif
