#include "NotifyOnUserEntry.h"
#include "../DataUpdates/UserEntryUpdate.h"

namespace simu5g {

NotifyOnUserEntry::NotifyOnUserEntry(RavensControllerApp *controllerApp) : DataHandlerPolicyBase(controllerApp) {
    // we are going to use the controllerApp_ instance from DataHandlerPolicyBase
    // start standby as an empty list
    standby.clear();
    speedInfoList.clear();
}

NotifyOnUserEntry::~NotifyOnUserEntry() {
    // clear the standby list
    standby.clear();
    speedInfoList.clear();
}

inet::Packet *NotifyOnUserEntry::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{

    // it might be a good idea to return a message anyway since we don't know what the future holds
    inet::Packet *pck = nullptr;

    auto updated_snapshot = received_packet;

    for (auto it = speedInfoList.begin(); it != speedInfoList.end(); ) {
    if (simTime() - it->second.timestamp > 4) {
        std::cout << simTime() << " - NotifyOnUserEntry::handleDataMessage - user: " << it->first << " was removed from the speedInfoList" << std::endl;
        it = speedInfoList.erase(it); // Erase and move to the next element
    } else {
        ++it;
    }
}

    for (const auto &user : updated_snapshot->getUsers())
    {
        auto it_speed = speedInfoList.find(user.first);
        if (it_speed == speedInfoList.end())
        {
            // new user -> add it to the speedInfoList
            UeSpeedInfo speedInfo = UeSpeedInfo(user.first, user.second.getCurrentLocation().getHorizontalSpeed(), user.second.getAccessPointId(), simTime());
            speedInfoList.insert({user.first, speedInfo});
        }
        else
        {
        
            // update the speedInfoList
            it_speed->second.bsId = user.second.getAccessPointId();
            it_speed->second.speed = user.second.getCurrentLocation().getHorizontalSpeed();
            it_speed->second.timestamp = simTime();
            
        }
    }
    
    for (const auto &user : updated_snapshot->getUsers())
    {
        auto it_users = standby.find(user.first);
        if (it_users == standby.end())
        {
            // first we need to calculate the NumberOfUsers, AvgSpeed and NumberOfUsersLessSpeed
            int numberOfUsers = calculateNumberOfUsers(user.second.getAccessPointId());
            double avgSpeed = calculateAvgSpeed(user.second.getAccessPointId());
            int numberOfUsersLessSpeed = calculateNumberOfUsersLessSpeed(user.second.getAccessPointId(), user.second.getCurrentLocation().getHorizontalSpeed());
            // new user -> add it to the stanby list and to the userEntryUpdates list
            UserEntryUpdate userEntryUpdate = UserEntryUpdate(user.second.getAddress(), updated_snapshot->getMecHostId(), "0", updated_snapshot->getTimeStamp(), user.second.getAccessPointId(), user.first, user.second.getCurrentLocation().getX(), user.second.getCurrentLocation().getY(), user.second.getCurrentLocation().getHorizontalSpeed(), user.second.getCurrentLocation().getBearing(), user.second.getDistanceToAP(), numberOfUsers, avgSpeed, numberOfUsersLessSpeed);
            // add the user to the stanby list
            standby.insert({user.first, userEntryUpdate});
            addUserEntryUpdate(userEntryUpdate);
            //std::cout << simTime() << " - NotifyOnUserEntry::handleDataMessage - user: " << user.first << " that is using BS: " << userEntryUpdate.getAccessPointId() << " was added to the standby list" << endl;
        }
        else
        {
            // user is on the stanby list -> check if the BS or MEH are the same
            if (it_users->second.getAccessPointId() != user.second.getAccessPointId())
            {
                int numberOfUsers = calculateNumberOfUsers(user.second.getAccessPointId());
                double avgSpeed = calculateAvgSpeed(user.second.getAccessPointId());
                int numberOfUsersLessSpeed = calculateNumberOfUsersLessSpeed(user.second.getAccessPointId(), user.second.getCurrentLocation().getHorizontalSpeed());
                // new user -> add it to the stanby list and to the userEntryUpdates list
                UserEntryUpdate userEntryUpdate = UserEntryUpdate(user.second.getAddress(), updated_snapshot->getMecHostId(), "0", updated_snapshot->getTimeStamp(), user.second.getAccessPointId(), user.first, user.second.getCurrentLocation().getX(), user.second.getCurrentLocation().getY(), user.second.getCurrentLocation().getHorizontalSpeed(), user.second.getCurrentLocation().getBearing(), user.second.getDistanceToAP(), numberOfUsers, avgSpeed, numberOfUsersLessSpeed);
                addUserEntryUpdate(userEntryUpdate);
                standby.erase(it_users);
                standby.insert({user.first, userEntryUpdate});
                //std::cout << simTime() << " - NotifyOnUserEntry::handleDataMessage - user: " << user.first << " that is using BS: " << userEntryUpdate.getAccessPointId() << " was in the stanby list and was updated" << endl;
            }
        } 
    }

    return pck;
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

int NotifyOnUserEntry::calculateNumberOfUsers(std::string baseStationId)
{
    // calculate the number of users in the base station
    int numberOfUsers = 0;
    // we should use the speedInfoList map to calculate the number of users of the base station
    for (auto &user : speedInfoList)
    {
        if (user.second.bsId == baseStationId)
        {
            numberOfUsers++;
        }
    }
    return numberOfUsers;
}

double NotifyOnUserEntry::calculateAvgSpeed(std::string baseStationId)
{
    // calculate the average speed of the users in the base station
    double avgSpeed = 0;
    int numberOfUsers = 0;
    // we should use the speedInfoList map to calculate the average speed of the users of the base station
    for (auto &user : speedInfoList)
    {
        if (user.second.bsId == baseStationId)
        {
            avgSpeed += user.second.speed;
            numberOfUsers++;
        }
    }
    if (numberOfUsers > 0)
    {
        avgSpeed = avgSpeed / numberOfUsers;
    }
    return avgSpeed;
}

int NotifyOnUserEntry::calculateNumberOfUsersLessSpeed(std::string baseStationId, long speed)
{
    // calculate the number of users in the base station with speed less than the given speed
    int numberOfUsers = 0;
    // we should use the speedInfoList map to calculate the number of users of the base station with speed less than the given speed
    for (auto &user : speedInfoList)
    {
        if (user.second.bsId == baseStationId && user.second.speed < speed)
        {
            numberOfUsers++;
        }
    }
    return numberOfUsers;
}

}