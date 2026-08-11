#ifndef RAVENS_CONTROLLER_APP_USEREVENT_H_
#define RAVENS_CONTROLLER_APP_USEREVENT_H_

#include "apps/mec/RavensApps/RavensControlProtocol.h"

#include <string>
#include <sstream>
#include "omnetpp.h"

namespace simu5g {

// One confirmed change in where a user is, as the Controller concluded it.
//
// Replaces UserMEHUpdate, which described the same thing as a pair of host names
// and left the receiver to work out what had happened from which of them was
// empty. Every consumer wrote the same three-way if-chain, and each wrote it
// slightly differently.
//
// Two things are new. eventType says what happened instead of implying it. And
// observedAt carries when the Agent saw the change, which the event hooks have
// always known and always discarded — DecisionLogger wants exactly this to
// measure how stale a decision's input was, rather than inferring it from the
// configured intervals.
//
// These are sent one per message, the moment they are confirmed. There is no
// container of pending events anywhere: the Controller used to accumulate them
// in a map keyed by user and drain it every two seconds, so a user that moved
// twice inside one window lost its first move.
struct UserEvent
{
    std::string ueAddress;

    // USER_ENTRY, USER_HANDOVER or USER_EXIT. Held as an int because that is
    // what crosses the message boundary; compare it against the enum.
    int eventType = USER_ENTRY;

    // ENTRY leaves fromMEHId empty, EXIT leaves toMEHId empty, HANDOVER sets
    // both. Read eventType rather than testing these for emptiness.
    std::string fromMEHId;
    std::string toMEHId;

    // When the Agent first detected the change, not when the Controller
    // forwarded it. The two differ by the reporting delay, plus — for an exit —
    // the whole confirmation window, which is the larger part by far.
    omnetpp::simtime_t observedAt = -1;

    // Required because this travels as a field of UserEventMessage: the message
    // compiler needs a way to render an existing class it cannot introspect.
    // Also what the Qtenv inspector shows for the field, so it is worth keeping
    // readable.
    std::string str() const
    {
        std::ostringstream out;
        out << userEventTypeName(eventType) << " " << ueAddress
            << " '" << fromMEHId << "' -> '" << toMEHId << "'"
            << " observed at " << observedAt;
        return out.str();
    }
};

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_USEREVENT_H_ */
