#include "SaveDataHistory.h"

namespace simu5g {

SaveDataHistory::SaveDataHistory(RavensControllerApp* controllerApp, std::string path):LocationDataHandlerPolicyBase(controllerApp)
{
    // 1. Mobility File (Standard Vectors)
    std::string name = path+"run_"+std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber())+"_mobility.csv"; 
    csvFile.open(name, std::ios::out | std::ios::trunc);
    csvFile << "Timestamp,UEId,MEHId,AccessPointId,x,y,z,Speed,Bearing,DistanceToAccessPoint" << endl;
    
    // 2. Lifecycle File (Events)
    std::string lifecycleName = path+"run_"+std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber())+"_lifecycle.csv"; 
    lifecycleFile.open(lifecycleName, std::ios::out | std::ios::trunc);
    lifecycleFile << "Timestamp,EventType,UEId,Details" << endl;
    
    EV << "SaveDataHistory initialized. Mobility: " << name << ", Lifecycle: " << lifecycleName << endl;
}

/*
* handleDataMessage is called when a RavensLinkUsersInfoSnapshotMessage is received
* this message contains the information about the users connected to the access point
* 1) We need to update the mehStateMap with the new users (this means remove inactive users 
*    and add/update users to the userStateMap)
* 2) for the current strategy, we also want to save the data in a csv file
*/
inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    inet::Packet* pck = nullptr; // Initialize pck to nullptr as before

    // A. Log Mobility (Source of Truth for this timestamp)
    for(const auto& userPair : received_packet->getUsers()){
        const auto& userData = userPair.second;
        csvFile << received_packet->getTimeStamp() << "," 
                << userPair.first << "," 
                << received_packet->getMecHostId() << "," 
                << userData.getAccessPointId() << "," 
                << userData.getCurrentLocation().getX() << "," 
                << userData.getCurrentLocation().getY() << "," 
                << userData.getCurrentLocation().getZ() << "," 
                << userData.getCurrentLocation().getHorizontalSpeed() << "," 
                << userData.getCurrentLocation().getBearing() << "," 
                << userData.getDistanceToAP() << endl;
    }
    // csvFile.flush(); // Moved to periodic flush

    // B. Detect Entries & Handovers (Compare Packet vs Existing Map)
    for(const auto& userPair : received_packet->getUsers()){
        const std::string& ueId = userPair.first;
        const auto& newData = userPair.second;
        std::string newMehId = received_packet->getMecHostId();
        std::string newApId = newData.getAccessPointId();

        auto it = controllerApp_->userStateMap.find(ueId);
        if(it == controllerApp_->userStateMap.end()) {
            // New User -> ENTRY
            lifecycleFile << received_packet->getTimeStamp() << ",ENTRY," << ueId << "," << newMehId << endl;
        } else {
            // Existing User -> CHECK FOR CHANGES
            const auto& oldState = it->second;
            
            // MEH Handover
            if(oldState.currentMEH != newMehId) {
                lifecycleFile << received_packet->getTimeStamp() << ",HANDOVER_MEH," << ueId << "," << oldState.currentMEH << "->" << newMehId << endl;
            }
            // AP Handover
            if(oldState.userData.getAccessPointId() != newApId) {
                 lifecycleFile << received_packet->getTimeStamp() << ",HANDOVER_AP," << ueId << "," << oldState.userData.getAccessPointId() << "->" << newApId << endl;
            }
        }
    }

    // C. Update Map (Apply new data)
    controllerApp_->updateUserStateMap(received_packet);

    // D. Remove Inactive & Log Exits (Clean up timeouts)
    std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
    for(const auto& user : removedUsers) {
        lifecycleFile << simTime() << ",EXIT," << user.userId << "," << user.currentMEH << endl;
    }
    // lifecycleFile.flush(); // Moved to periodic flush

    // Periodic Flush
    msgCount_++;
    if (msgCount_ >= FLUSH_INTERVAL_) {
        csvFile.flush();
        lifecycleFile.flush();
        msgCount_ = 0;
    }

    return pck;
}

SaveDataHistory::~SaveDataHistory()
{
    csvFile.close();
    lifecycleFile.close();
}

}
