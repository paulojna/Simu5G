#include "NotifyOnDataChange.h"

namespace simu5g {

NotifyOnDataChange::NotifyOnDataChange(RavensControllerApp *controllerApp)
    : LocationDataHandlerPolicyBase(controllerApp)
{
}

// No telemetry hooks: this policy runs its Agents in event-only mode, so no
// telemetry frame is ever sent to it. Placement changes arrive entirely through
// the event hooks below.
void NotifyOnDataChange::onUserEntry(const std::string& userId, const std::string& meh,
                                     int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
{
    EV << "NotifyOnDataChange::onUserEntry - " << userId << " at " << meh << endl;
    emitUserUpdate(userId, "", meh);
}

void NotifyOnDataChange::onUserHandover(const std::string& userId,
                                        const std::string& fromMeh, const std::string& toMeh,
                                        int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
{
    EV << "NotifyOnDataChange::onUserHandover - " << userId << " " << fromMeh << " -> " << toMeh << endl;
    emitUserUpdate(userId, fromMeh, toMeh);
}

void NotifyOnDataChange::onUserExit(const std::string& userId, const std::string& fromMeh,
                                    int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
{
    EV << "NotifyOnDataChange::onUserExit - " << userId << " left " << fromMeh << endl;
    emitUserUpdate(userId, fromMeh, "");
}

}
