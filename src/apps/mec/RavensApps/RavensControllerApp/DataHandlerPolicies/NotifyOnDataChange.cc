#include "NotifyOnDataChange.h"
#include "../DataUpdates/UserMEHUpdate.h"

#include <unordered_map>

#define USERS_UPDATE 7

namespace simu5g {

NotifyOnDataChange::NotifyOnDataChange(RavensControllerApp *controllerApp, int treshold) : DataHandlerPolicyBase(controllerApp)
{
    stanby_treshold_ = treshold;
    controllerApp_->hostsDataHistory[simTime()] = controllerApp_->hostsData;
    EV << "NotifyOnDataChange::NotifyOnDataChange - max_iterations = " << max_iterations << endl;
}

inet::Packet *NotifyOnDataChange::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    /*
        This strategy aims to fill the userUpdates list and update the hostsData map at the RavensControllerApp.
        1) Run through stanby to check if any user there has passed the time
        2) Run through the received snapshot and check:
            a) if any UE changed from one MEH to another
            b) if any UE got out of the system
            c) if any UE entered the system
            For each update, check if the userUpdates has info about that specific UE and update if it is the case. If not, new entry.
        3) Update the hostsData map with the received information.
    */

    // it might be a good idea to return a message anyway since we don't know what the future holds
    inet::Packet *pck = nullptr;

    // comparing received_packet with the correspondent element of the hostsData map
    auto updated_snapshot = received_packet;

    // 1) check if there are any UEs in stanby in which the time_limit passed, if so removed them from the list
    simtime_t timestamp = simTime();
    if (standby.size() > 0)
    {
        for (auto it = standby.begin(); it != standby.end();)
        {
            auto &standby_user = *it;
            if (timestamp >= standby_user.second.time_limit)
            {
                EV << "NotifyOnDataChange::handleDataMessage - user " << standby_user.first << " has passed the time limit!" << endl;

                // add that to the userUpdates list in the controllerApp
                UserMEHUpdate userUpdate(standby_user.first, standby_user.second.meh, " ");
                EV << "NotifyOnDataChange::handleDataMessage - user " << userUpdate.getAddress() << " is going to the userUpdates list" << endl;
                addUserUpdate(userUpdate);

                it = standby.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    auto &host_info = controllerApp_->hostsData[updated_snapshot->getMecHostId()];
    std::map<std::string, UserData> user_list = host_info.getUsers();
    //  run through the list of users in the received snapshot
    for (auto &user : updated_snapshot->getUsers())
    {
        // check if the user is in the same MEH as the one in the hostsData map
        auto it_users = user_list.find(user.first);
        if (it_users == user_list.end())
        {
            //  not found -> is it in the standby list?
            auto it_standby = standby.find(user.first);
            if (it_standby != standby.end())
            {
                // found -> is the MEH the same or is it different?
                if (it_standby->second.meh != updated_snapshot->getMecHostId())
                {
                    // different -> create update and remove from the standby list
                    UserMEHUpdate userUpdate = UserMEHUpdate(user.first, it_standby->second.meh, updated_snapshot->getMecHostId());
                    addUserUpdate(userUpdate);
                    standby.erase(it_standby);
                }
                else
                {
                    // same -> remove from the standby list
                    standby.erase(it_standby);
                }
            }
            else
            {
                // not found -> it might be a new user, but first we need to check if it is not present on the other MEHs of the hostsData map (old data)
                bool found = false;
                std::string user_found;
                for (auto &host : controllerApp_->hostsData)
                {
                    if (host.first != updated_snapshot->getMecHostId())
                    {
                        auto other_host_info_users = host.second.getUsers();
                        auto it = other_host_info_users.find(user.first);
                        if (it != other_host_info_users.end())
                        {
                            EV << " We found user " << user.first << " in another MEH " << host.first << endl;
                            user_found = host.first;
                            found = true;
                            break;
                        }
                    }
                }

                if(!found)
                {
                    // new user -> add it to the hostsData map
                    UserMEHUpdate userUpdate = UserMEHUpdate(user.first, " ", updated_snapshot->getMecHostId());
                    addUserUpdate(userUpdate);
                }
                else
                {
                    // we found it on another MEH -> we need to add that to the userUpdates list
                    UserMEHUpdate userUpdate = UserMEHUpdate(user.first, user_found, updated_snapshot->getMecHostId());
                    addUserUpdate(userUpdate);

                    //check if the user was on the standby list and remove it if so
                    auto it_standby = standby.find(user.first);
                    if (it_standby != standby.end())
                    {
                        standby.erase(it_standby);
                        EV << "NotifyOnDataChange::handleDataMessage - user " << user.first << " was removed from the standby list" << endl;
                    }

                    //remove it also from the other MEH that is on the hostsData map
                    auto other_host_info_users = controllerApp_->hostsData[user_found].getUsers();
                    auto it = other_host_info_users.find(user.first);
                    if (it != other_host_info_users.end())
                    {
                        other_host_info_users.erase(it);
                        EV << "NotifyOnDataChange::handleDataMessage - user " << user.first << " was removed from the other MEH " << user_found << endl;
                    }
                    controllerApp_->hostsData[user_found].setUsers(other_host_info_users);
                }
            }
        }
        else
        {
            //  found -> we need to remove it from the hostsData map
            user_list.erase(it_users);
        }
    }

    // run through the users in it_host and add them to the standby list
    for (auto &old_user : user_list)
    {
        ueStanbyElement standby_user(old_user.first, simTime() + stanby_treshold_, updated_snapshot->getMecHostId());
        standby.insert({old_user.first, standby_user});
        EV << "NotifyOnDataChange::handleDataMessage - user " << old_user.first << " was added to the standby list when he was on meh" << updated_snapshot->getMecHostId() << endl;
    }

    // 3) update the hostsData map with the received information
    controllerApp_->hostsData[updated_snapshot->getMecHostId()].setUsers(updated_snapshot->getUsers());
    controllerApp_->hostsData[updated_snapshot->getMecHostId()].setLastUpdated(simTime());

    return pck;
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
