#include "NotifyOnDataChange.h"
#include "../DataUpdates/UserMEHUpdate.h"

#include <unordered_map>

namespace simu5g {

NotifyOnDataChange::NotifyOnDataChange(RavensControllerApp *controllerApp, int treshold) : LocationDataHandlerPolicyBase(controllerApp)
{

}

/*
* handleDataMessage is called when a RavensLinkUsersInfoSnapshotMessage is received
* this message contains the information about the users connected to the access point
* 1) we need to run the removeInactiveUsere and use the return value to add user updates of type exit 
* these are the ones with the NewMehId = "" and the LastMehId = the MEH the user was connected to
* 2) we need to understand which changes happened. For that we should compare the data in 
* the message with the data in the userStateMap -> which users entered and which changed MEH?
* 3) finnaly, we should update the userStateMap with the new data
*/
inet::Packet *NotifyOnDataChange::handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
    inet::Packet* pck = nullptr;

    // C1 safety net: purge users absent for longer than threshold_ (inactivity, not normal EXIT)
    std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
    for (auto &user : removedUsers)
    {
        UserMEHUpdate update;
        update.setLastMEHId(user.currentMEH);
        update.setNewMEHId("");
        update.setAddress(user.userId);
        addUserUpdate(update);
    }

    // Entry/exit detection and handover decisions moved to handleEventFrame() (eRAVENS R1)
    return pck;
}

void NotifyOnDataChange::addUserUpdate(UserMEHUpdate &update)
{
    EV << "NotifyOnDataChange::addUserUpdate - user " << update.getAddress() << " was sent to be added to the userUpdates map" << endl;

    // PERFORMANCE IMPROVEMENT: O(1) insert/update using map instead of O(n) linear search
    // Original linear search code commented out for reference:
    // for (auto &userUpdate : controllerApp_->userUpdates) {
    //     if (userUpdate.getAddress() == update.getAddress()) {
    //         userUpdate.setLastMEHId(update.getLastMEHId());
    //         userUpdate.setNewMEHId(update.getNewMEHId());
    //         return;
    //     }
    // }
    // controllerApp_->userUpdates.push_back(update);

    const std::string& address = update.getAddress();
    auto [it, inserted] = controllerApp_->userUpdates.insert_or_assign(address, update);

    if (inserted) {
        EV << "NotifyOnDataChange::addUserUpdate - user " << address << " added to the userUpdates map" << endl;
    } else {
        EV << "NotifyOnDataChange::addUserUpdate - user " << address << " was updated in the userUpdates map" << endl;
    }
}

}
