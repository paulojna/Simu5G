

#ifndef _APPS_MEC_MEAPPS_RAAPP_H_
#define _APPS_MEC_MEAPPS_RAAPP_H_

#include <cstddef>
#include <iterator>
#include <map>
#include <vector>

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

    simtime_t telemetryInterval_;  // received from the Controller in the handshake ACK
    int agentMode_;             // EVENT_ONLY_MODE, TELEMETRY_ONLY_MODE, or FULL_MODE

    int entryConfirmSamples_;  // consecutive present LS samples required to confirm ENTRY
    int exitConfirmSamples_;   // consecutive absent LS samples required to confirm EXIT

    // Per-UE event detection state. Lives across LS ticks (unlike the old
    // frame-boundary pending maps) so consecutive-sample counts survive between
    // samples. An entry is created the first time a UE is observed and erased
    // either when its EXIT is confirmed and sent, or — if it was never reported —
    // the moment it disappears, so a later reappearance starts fresh.
    struct UeEventState {
        simtime_t firstDetectedAt;   // start of the current present or absent streak
        int consecutivePresent = 0;
        int consecutiveAbsent  = 0;
        bool reported = false;       // true once ENTRY has been sent to the Controller
    };
    std::unordered_map<std::string, UeEventState> eventState_;

    // Events that crossed their threshold but could not be sent yet because the
    // signaling channel was down. Retried on the next LS tick, whether or not
    // that tick produces new events of its own.
    RavensEventList pendingUnsent_;

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

    // Current state: where each UE is right now, overwritten every Location
    // Service tick. Used for the event detection above and for the cell-level
    // averages. A UE is erased from here the moment it stops being reported.
    std::unordered_map<std::string, UserData> users;

    // Observations waiting to go out in the next telemetry frame, keyed by UE.
    // The Location Service is read once a second while frames leave less often,
    // so each UE normally accumulates several samples between frames; sending
    // them all is what keeps a trajectory continuous instead of subsampled.
    //
    // Deliberately separate from the users map above, and with a different
    // lifetime: a sample is copied in when observed and stays until a frame
    // carries it away, so erasing a departed UE from users does not discard the
    // observations it already produced. Those final observations - a UE on its
    // way out of the cell - are the ones the handover models most need.
    //
    // Ordered (not hashed) so groups leave in a stable address order, which
    // keeps the resulting rows consistent from run to run.
    std::map<std::string, std::vector<UserData>> sampleBuffer_;

    // How many telemetry frames this Agent sent, and how many of those were
    // large enough that the network layer had to split them up. Recorded as
    // result scalars in finish(), so "does this actually happen, and how often?"
    // is answered by a number per run rather than by searching the logs.
    long telemetryFramesSent_ = 0;
    long telemetryFramesOversized_ = 0;

    // Cell readings waiting to go out in the next telemetry frame, oldest first.
    // The RNIS reports once a second while frames leave less often, so each frame
    // normally carries several — the same relationship sampleBuffer_ has with the
    // Location Service, and emptied in the same place.
    //
    // A whole record per notification, rather than one record written into over
    // and over. That is what keeps a value from outliving the reading it came
    // from: a field the notification does not mention sits at its "not measured"
    // default in that record, because there is nothing older for it to inherit.
    std::vector<AccessPointRadioInfoData> cellSampleBuffer_;

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
    void sendEventFrame(const RavensEventList& newEvents);  // sends newEvents plus any retry backlog (TCP)
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
