#ifndef _APPS_MEC_RAVENSAPPS_RAVENSCONTROLPROTOCOL_H_
#define _APPS_MEC_RAVENSAPPS_RAVENSCONTROLPROTOCOL_H_

// RavensControl vocabulary (Controller <-> MEC orchestrator), shared by both ends
// so the two sides cannot drift. The values travel as plain ints in
// RavensControlPacket.type and UserEventMessage.eventType (see
// RavensControlPacket.msg).
//
// This is the Controller -> orchestrator counterpart of RavensLinkProtocol.h,
// which does the same job for Agent -> Controller. Both enums were previously
// pairs of #defines, one copy in RavensControllerApp.cc and another in
// MecOrchestrator.cc, each carrying a comment asking the reader to keep them in
// step by hand.

namespace simu5g {

// The three streams the Controller publishes. They are independent: a profile is
// which of them are switched on, not a single mode name.
//
// They also differ in timing, and for a reason particular to each. User events
// and predictions are sent the moment the Controller has them, because both are
// on the critical path — an event is how a wrong prediction gets corrected, and
// every second a prediction spends waiting is a second further ahead the model
// must have predicted. Telemetry is batched into a window instead, because a
// learning agent wants one coherent round of observations more than it wants
// them a second sooner.
//
// Numeric values are kept from the #define era so nothing silently changes
// meaning; the gap before 20 is historical.
enum RavensControlFrameType {
    USER_EVENT        = 20,  // one confirmed placement change, sent immediately
    PREDICTION_REPORT = 21,  // expected future changes, sent as produced
    TELEMETRY_REPORT  = 22   // one window of raw observations
};

// What a user event reports. Which of fromMEHId / toMEHId are set follows from
// this, rather than the receiver inferring the event from which of them happens
// to be empty: ENTRY has no from, EXIT has no to, HANDOVER has both.
//
// Deliberately a separate enum from RavensEventType in RavensLinkProtocol.h,
// despite sharing two of its words. An Agent's EXIT means "this host stopped
// seeing the user"; the event here means "the user left the system". The gap
// between those two claims is exactly what exitConfirmationWindow exists to
// bridge, and one shared enum would hide it. There is also no HANDOVER on the
// Agent side: no single host can observe one.
enum UserEventType {
    USER_ENTRY    = 0,
    USER_HANDOVER = 1,
    USER_EXIT     = 2
};

inline const char *userEventTypeName(int eventType)
{
    switch (eventType) {
        case USER_ENTRY:    return "ENTRY";
        case USER_HANDOVER: return "HANDOVER";
        case USER_EXIT:     return "EXIT";
    }
    return "UNKNOWN";
}

} // namespace simu5g

#endif
