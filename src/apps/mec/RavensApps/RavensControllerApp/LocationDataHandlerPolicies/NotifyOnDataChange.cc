#include "NotifyOnDataChange.h"
#include "../DataUpdates/UserMEHUpdate.h"

#include <unordered_map>

#define USERS_UPDATE 7

namespace simu5g {

NotifyOnDataChange::NotifyOnDataChange(RavensControllerApp *controllerApp, int treshold) : LocationDataHandlerPolicyBase(controllerApp)
{
    stanby_treshold_ = treshold;
    //controllerApp_->hostsDataHistory[simTime()] = controllerApp_->hostsData;
    EV << "NotifyOnDataChange::NotifyOnDataChange - max_iterations = " << max_iterations << endl;
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
        auto userIt = controllerApp_->userStateMap.find(user.first);
        if (userIt == controllerApp_->userStateMap.end())
        {
            // add the user to the userUpdates list
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
                // add the user to the userUpdates list
                UserMEHUpdate update;
                update.setLastMEHId(userIt->second.currentMEH);
                update.setNewMEHId(updatedSnapshot->getMecHostId());
                update.setAddress(user.second.getAddress());
                addUserUpdate(update);
            }
        }
    }

    // update the userStateMap
    controllerApp_->updateUserStateMap(received_packet);

    // return nullptr since we don't need to send any packet
    return nullptr;
}

void NotifyOnDataChange::addUserUpdate(UserMEHUpdate &update)
{
    EV << "NotifyOnDataChange::addUserUpdate - user " << update.getAddress() << " was sent to be added to the userUpdates list" << endl;

    // check if the user is already in the list, if so update the values
    for (auto &userUpdate : controllerApp_->userUpdates)
    {
        if (userUpdate.getAddress() == update.getAddress())
        {
            userUpdate.setLastMEHId(update.getLastMEHId());
            userUpdate.setNewMEHId(update.getNewMEHId());
            EV << "NotifyOnDataChange::addUserUpdate - user " << update.getAddress() << " was updated in the userUpdates list" << endl;
            return;
        }
    }

    // if the user is not in the list, add it
    controllerApp_->userUpdates.push_back(update);
    EV << "NotifyOnDataChange::addUserUpdate - user " << update.getAddress() << " added to the userUpdates list" << endl;
}

}
