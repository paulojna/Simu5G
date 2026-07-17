#include "NotifyOnDataChange.h"

namespace simu5g {

NotifyOnDataChange::NotifyOnDataChange(RavensControllerApp *controllerApp)
    : LocationDataHandlerPolicyBase(controllerApp)
{
}

inet::Packet* NotifyOnDataChange::handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
    // C1 safety net: purge users absent longer than threshold_ — not a normal exit
    std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
    for (const auto& user : removedUsers)
        onUserExit(user.userId, user.currentMEH, -1, SIMTIME_ZERO);

    return nullptr;
}

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
