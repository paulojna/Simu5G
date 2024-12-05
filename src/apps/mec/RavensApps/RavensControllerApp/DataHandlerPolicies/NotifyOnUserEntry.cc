#include "NotifyOnUserEntry.h"
#include "../DataUpdates/UserEntryUpdate.h"

namespace simu5g {

NotifyOnUserEntry::NotifyOnUserEntry(RavensControllerApp *controllerApp) : DataHandlerPolicyBase(controllerApp) {
    // we are going to use the controllerApp_ instance from DataHandlerPolicyBase
    // start standby as an empty list
    standby.clear();
}

NotifyOnUserEntry::~NotifyOnUserEntry() {
    // clear the standby list
    standby.clear();
}

inet::Packet *NotifyOnUserEntry::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    /*
        This strategy aims to send updates regarding new entrances on Base Stations/MEH's. We are going to use the stanby list just to check if a user changed BS 
        or MEH. If so, we are going to add it to the userEntryUpdates list and update the standby list. The goal is to inform the MEO whenever we detect a new user
        in a new BS. WE ONLY WANT TO SEND THE FIRST VALUE (therefore userENTRYUpdates).
        1) Run through stanby and check if any user on received_packet is there
            a) If not, add it to the stanby list and userEntryUpdates list with all the information
            b) If it is check if the BS or MEH are the same
                b-1) If they are, do nothing
                b-2) If they are not, add it to the userEntryUpdates list with all the information and update the standby list   
    */

    // it might be a good idea to return a message anyway since we don't know what the future holds
    inet::Packet *pck = nullptr;

    auto updated_snapshot = received_packet;

    // Lets check if there are any UEs in stanby that are also on updated_snapshot
    for (auto &user : updated_snapshot->getUsers())
    {
        auto it_users = standby.find(user.first);
        if (it_users == standby.end())
        {
            // new user -> add it to the stanby list and to the userEntryUpdates list
            UserEntryUpdate userEntryUpdate = UserEntryUpdate(user.second.getAddress(), updated_snapshot->getMecHostId(), "0", updated_snapshot->getTimeStamp(), user.second.getAccessPointId(), user.first, user.second.getCurrentLocation().getX(), user.second.getCurrentLocation().getY(), user.second.getCurrentLocation().getHorizontalSpeed(), user.second.getCurrentLocation().getBearing(), user.second.getDistanceToAP());
            // add the user to the stanby list
            standby.insert({user.first, userEntryUpdate});
            addUserEntryUpdate(userEntryUpdate);
            EV << "NotifyOnUserEntry::handleDataMessage - user: " << user.first << " that is using BS: " << userEntryUpdate.getAccessPointId() << " was added to the standby list" << endl;
        }
        else
        {
            // user is on the stanby list -> check if the BS or MEH are the same
            if (it_users->second.getAccessPointId() != user.second.getAccessPointId())
            {
                // user changed BS -> add it to the userEntryUpdates list and update the stanby list
                UserEntryUpdate userEntryUpdate = UserEntryUpdate(user.second.getAddress(), updated_snapshot->getMecHostId(), "0", updated_snapshot->getTimeStamp(), user.second.getAccessPointId(), user.first, user.second.getCurrentLocation().getX(), user.second.getCurrentLocation().getY(), user.second.getCurrentLocation().getHorizontalSpeed(), user.second.getCurrentLocation().getBearing(), user.second.getDistanceToAP());
                addUserEntryUpdate(userEntryUpdate);
                standby.erase(it_users);
                standby.insert({user.first, userEntryUpdate});
                EV << "NotifyOnUserEntry::handleDataMessage - user: " << user.first << " that is using BS: " << userEntryUpdate.getAccessPointId() << " was in the stanby list and was updated" << endl;
            }
        } 
    }
}

void NotifyOnUserEntry::addUserEntryUpdate(UserEntryUpdate &update) {
    // add the user to the controllerApp_->userEntryUpdates list
    // check if the user is already in the controllerApp_->userEntryUpdates, if so update the values
    for (auto &userUpdate : controllerApp_->userEntryUpdates)
    {
        if (userUpdate.getUeId() == update.getUeId())
        {
            userUpdate = update;
            EV << "NotifyOnUserEntry::addUserEntryUpdate - user: " << update.getUeId() << " was updated in the userEntryUpdates list" << endl;
            return;
        }
    }

    // if the user is not in the list, add it
    controllerApp_->userEntryUpdates.push_back(update);
    EV << "NotifyOnUserEntry::addUserEntryUpdate - user: " << update.getUeId() << " was added to the userEntryUpdates list" << endl;
}

}