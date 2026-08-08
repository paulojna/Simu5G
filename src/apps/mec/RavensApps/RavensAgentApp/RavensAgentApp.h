

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

    // How often the Location Service is asked to report, in seconds. Sent in the
    // subscription request and used to work out how many observations make up a
    // telemetry frame, so the two cannot disagree about the sensing rate.
    //
    // This is the finest resolution anything downstream can have. One second is
    // the SUMO step: nothing in the mobility trace changes faster, so asking more
    // often would observe the same position twice.
    static constexpr int locationSamplingPeriodSeconds_ = 1;

    simtime_t telemetryInterval_;  // received from the Controller in the handshake ACK
    int agentMode_;             // EVENT_ONLY_MODE, TELEMETRY_ONLY_MODE, or FULL_MODE

    // The telemetry cadence, counted in Location Service notifications rather than
    // held on a clock of its own. A frame goes out on every observationsPerFrame_-th
    // notification, carrying that notification's observations.
    //
    // Counting rather than timing is what makes the cadence exact. Two independent
    // periodic clocks agree only as long as nothing is late; one notification
    // arriving after the frame timer would have the frame repeat the previous
    // observation and skip this one, leaving a duplicate timestamp and a gap in
    // the trajectory. Counting cannot produce either: the frame is late instead,
    // and the observations stay evenly spaced.
    //
    // Evenly spaced observations are also what lets a coarser dataset be derived
    // from a finer one offline — every third row of a one-second corpus is exactly
    // what a three-second cadence carries, which only holds if "every third" is
    // guaranteed rather than usual.
    int observationsPerFrame_ = 1;
    int observationsSinceFrame_ = 0;

    // Works out observationsPerFrame_ from telemetryInterval_. Called wherever that
    // interval is set — at startup from the NED parameter, and again when the
    // Controller's handshake reply overrides it.
    void recomputeObservationsPerFrame();

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
    // Service tick. A UE is erased from here the moment it stops being reported,
    // so this map holds exactly the UEs seen at the most recent tick.
    //
    // That property is what makes it the telemetry source as well as the input
    // to event detection and the cell-level averages: a frame carries one
    // observation per UE present now, and a UE missing from the map is a UE that
    // belongs in no frame.
    std::unordered_map<std::string, UserData> users;

    // How many telemetry frames this Agent sent, and how many of those were
    // large enough that the network layer had to split them up. Recorded as
    // result scalars in finish(), so "does this actually happen, and how often?"
    // is answered by a number per run rather than by searching the logs.
    long telemetryFramesSent_ = 0;
    long telemetryFramesOversized_ = 0;

    // The newest cell reading, as a list holding one element or none. Empty until
    // the RNIS first replies, which is why it is a list rather than a plain field:
    // the opening frames of a run genuinely have no reading, and that is worth
    // saying rather than faking with a sentinel record.
    //
    // Replaced whole on each notification, never written into. That is what keeps
    // a value from outliving the reading it came from: a field the notification
    // does not mention sits at its "not measured" default, because there is
    // nothing older for it to inherit.
    std::vector<AccessPointRadioInfoData> latestCellReading_;

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
