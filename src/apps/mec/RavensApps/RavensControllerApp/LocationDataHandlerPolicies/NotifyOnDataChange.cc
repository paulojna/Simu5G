#include "NotifyOnDataChange.h"
#include "../DataUpdates/UserMEHUpdate.h"

#include <unordered_map>

#define USERS_UPDATE 7

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
inet::Packet *NotifyOnDataChange::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
	inet::Packet* pck = nullptr; // Initialize pck to nullptr as before

	// remove the inactive users
	std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();

    // add the removed users to the userUpdates list
    for (auto &user : removedUsers)
    {
        UserMEHUpdate update;
        update.setLastMEHId(user.currentMEH);
        update.setNewMEHId("");
        update.setAddress(user.userId);
        addUserUpdate(update);
    }

    // compare the received data with the data in the userStateMap
    auto updatedSnapshot = received_packet;

    // run through the users in the snapshot and check if they are in the userStateMap
    for (const auto &user : updatedSnapshot->getUsers())
    {
        // Only consider users with valid radio stats (attached to this MEC Host's cell)
        // Assuming -1 indicates invalid/no connection
        if(user.second.getDlNongbrDelayUe() == -1) {
            continue; 
        }

        auto userIt = controllerApp_->userStateMap.find(user.first);
        if (userIt == controllerApp_->userStateMap.end())
        {
            // New user - ALWAYS add to userUpdates (not a handover, no lockout check needed)
            UserMEHUpdate update;
            update.setLastMEHId("");
            update.setNewMEHId(updatedSnapshot->getMecHostId());
            update.setAddress(user.second.getAddress());
            addUserUpdate(update);
        }
        else
        {
            // user is in the map, let's check if the user has changed MEH
            if (userIt->second.currentMEH != updatedSnapshot->getMecHostId())
            {
                // This IS a handover - check if it will be accepted (ping-pong prevention)
                if (controllerApp_->shouldAcceptHandover(user.first, updatedSnapshot->getMecHostId()))
                {
                    UserMEHUpdate update;
                    update.setLastMEHId(userIt->second.currentMEH);
                    update.setNewMEHId(updatedSnapshot->getMecHostId());
                    update.setAddress(user.second.getAddress());
                    addUserUpdate(update);
                }
                else
                {
                    EV << "NotifyOnDataChange - Handover NOT added to userUpdates (lockout active for " << user.first << ")" << endl;
                }
            }
        }
    }
	// Update MEH State (Radio Info)
	controllerApp_->updateMehStateMap(received_packet);

    // update the userStateMap
    controllerApp_->updateUserStateMap(received_packet);

    // return nullptr since we don't need to send any packet
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
