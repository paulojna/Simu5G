#ifndef _APPS_MEC_RAVENSAPPS_RAVENSLINKPROTOCOL_H_
#define _APPS_MEC_RAVENSAPPS_RAVENSLINKPROTOCOL_H_

// RavensLink protocol vocabulary (Agent <-> Controller), shared by both ends
// so the two sides cannot drift. Values travel as plain ints in
// RavensLinkPacket.type, RavensLinkInfrastructureDetailsMessageAck.agentMode
// and RavensEvent.eventType (see RavensLinkPacket.msg).

namespace simu5g {

// Frame types. Numeric values kept from the #define era (gaps are historical).
enum RavensLinkFrameType {
    JOIN_NETWORK_REQUEST        = 0,  // Agent -> Controller (TCP signaling)
    JOIN_NETWORK_ACK            = 1,  // Controller -> Agent  (TCP signaling)
    INFRASTRUCTURE_DETAILS      = 2,  // Agent -> Controller (TCP signaling)
    INFRASTRUCTURE_DETAILS_ACK  = 3,  // Controller -> Agent  (TCP signaling)
    TELEMETRY_FRAME             = 6,  // Agent -> Controller (UDP telemetry) — periodic snapshot, loss-tolerant
    EVENT_FRAME                 = 8   // Agent -> Controller (TCP signaling) — ENTRY/EXIT batch, report-once
};

// Agent operating mode (set by Controller via INFRASTRUCTURE_DETAILS_ACK).
// Names describe which frame types the Agent emits — RAVENS is agnostic to how
// the Controller/orchestrator uses them.
enum RavensAgentMode {
    EVENT_ONLY_MODE     = 0,  // event frames only
    TELEMETRY_ONLY_MODE = 1,  // telemetry frames only (proactive-only; not yet wired)
    FULL_MODE           = 2   // event + telemetry frames
};

// Event subtypes (payload of EVENT_FRAME)
enum RavensEventType {
    EVENT_ENTRY = 0,
    EVENT_EXIT  = 1
};

} //namespace

#endif
