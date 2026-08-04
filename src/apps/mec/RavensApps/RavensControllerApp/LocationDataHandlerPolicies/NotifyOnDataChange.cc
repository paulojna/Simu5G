#include "NotifyOnDataChange.h"

namespace simu5g {

NotifyOnDataChange::NotifyOnDataChange(RavensControllerApp *controllerApp, int treshold)
    : LocationDataHandlerPolicyBase(controllerApp)
{
}

inet::Packet* NotifyOnDataChange::handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
    // Last-resort cleanup: drop users nothing has been heard about for far longer
    // than any normal gap. This is not how a departure is normally noticed —
    // exits are confirmed through the event channel — so this should stay quiet.
    // It exists so a user whose exit event was somehow never delivered cannot
    // linger indefinitely.
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
