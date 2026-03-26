#include "SaveDataHistory.h"
#include <sys/stat.h>
#include <sys/types.h>

namespace simu5g {

SaveDataHistory::SaveDataHistory(RavensControllerApp* controllerApp, std::string path):LocationDataHandlerPolicyBase(controllerApp)
{
    std::string runNumber = std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber());
    std::string dirPath = path + "run_" + runNumber + "/";

    // Create directory with read/write/search permissions for owner and group, and read/search for others
    if (mkdir(dirPath.c_str(), 0775) == -1) {
        if (errno != EEXIST) {
             EV << "Error creating directory " << dirPath << " : " << strerror(errno) << endl;
             // Fallback to original path if directory creation fails, or handle error appropriately
             // For now, assuming we proceed or it existed.
        }
    }

    // 1. User File (Standard Vectors + Radio Stats)
    std::string name = dirPath + "run_" + runNumber + "_users.csv";
    userFile.open(name, std::ios::out | std::ios::trunc);
    userFile << "TimestampSent, LastUpdated, LsLast, RnisLast, UEId,MEHId,AccessPointId,x,y,z,Speed,Bearing,DistanceToAccessPoint,DlDelay,DlPDR,DlDataVolume,UlDelay,UlPDR,UlDataVolume" << endl;
    
    // 2. Lifecycle File (Events)
    std::string lifecycleName = dirPath + "run_" + runNumber + "_lifecycle.csv"; 
    //lifecycleFile.open(lifecycleName, std::ios::out | std::ios::trunc);
    //lifecycleFile << "Timestamp,EventType,UEId,Details" << endl;

    // 3. Radio Stats File (DL/UL Usage and PDR)
    std::string radioStatsName = dirPath + "run_" + runNumber + "_radio_stats.csv";
    radioStatsFile.open(radioStatsName, std::ios::out | std::ios::trunc);
    radioStatsFile << "Timestamp,MEHId,CellId,DlPrbUsageCell,UlPrbUsageCell,DlNongbrPdrCell,UlNongbrPdrCell" << endl;
    
    EV << "SaveDataHistory initialized. Users: " << name << ", Lifecycle: " << lifecycleName << ", RadioStats: " << radioStatsName << endl;
}

inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    inet::Packet* pck = nullptr; // Initialize pck to nullptr as before

	std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
	for(const auto& user : removedUsers) {
		// even though we are not testing any QoS performance, it is better to remove the user from the MEH
		UserMEHUpdate update;
		update.setLastMEHId(user.currentMEH);
		update.setNewMEHId("");
		update.setAddress(user.userId);
		addUserUpdate(update);
		//lifecycleFile << simTime() << ",EXIT," << user.userId << "," << user.currentMEH << endl;
	}

    // A.1 Log Radio Stats
    const AccessPointRadioInfoData& apRadioInfo = received_packet->getApRadioInfo();
    if (!apRadioInfo.getAccessPointId().empty()) {
        radioStatsFile << received_packet->getTimeStamp() << ","
                       << received_packet->getMecHostId() << ","
                       << apRadioInfo.getAccessPointId() << ","
                       << apRadioInfo.getDlTotalPrbUsageCell() << ","
                       << apRadioInfo.getUlTotalPrbUsageCell() << ","
                       << apRadioInfo.getDlNongbrPdrCell() << ","
                       << apRadioInfo.getUlNongbrPdrCell() << endl;
    }

    // B. Log User Data (Source of Truth for this timestamp)
    for(const auto& userPair : received_packet->getUsers()){
        const auto& userData = userPair.second;
        userFile << received_packet->getTimeStamp() << ","
    			<< userData.getLastUpdated() << ","
    			<< userData.getLsUpdate() << ","
    			<< userData.getRnisUpdate() << ","
    	     << userPair.first << ","
                << received_packet->getMecHostId() << ","
                << userData.getAccessPointId() << ","
                << userData.getCurrentLocation().getX() << ","
                << userData.getCurrentLocation().getY() << ","
                << userData.getCurrentLocation().getZ() << ","
                << userData.getCurrentLocation().getHorizontalSpeed() << ","
                << userData.getCurrentLocation().getBearing() << ","
                << userData.getDistanceToAP() << ","
                << userData.getDlNongbrDelayUe() << ","
                << userData.getDlNongbrPdrUe() << ","
                << userData.getDlNongbrDataVolumeUe() << ","
                << userData.getUlNongbrDelayUe() << ","
                << userData.getUlNongbrPdrUe() << ","
                << userData.getUlNongbrDataVolumeUe() << endl;
    }
    // userFile.flush(); // Moved to periodic flush

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
			// New user - ALWAYS log ENTRY (not a handover, no lockout check needed)
			UserMEHUpdate update;
			update.setLastMEHId("");
			update.setNewMEHId(updatedSnapshot->getMecHostId());
			update.setAddress(user.second.getAddress());
			addUserUpdate(update);
			//lifecycleFile << received_packet->getTimeStamp() << ",ENTRY," << user.second.getAddress() << "," << updatedSnapshot->getMecHostId() << endl;
		}
		else
		{
			EV << "NotifyOnDataChange::addUserUpdate - user " << user.first << " possible handover situation " << endl;
			/*
			// user is in the map, let's check if the user has changed MEH
			if (userIt->second.currentMEH != updatedSnapshot->getMecHostId())
			{
				// This IS a handover - check if it will be accepted (ping-pong prevention)
				if (controllerApp_->shouldAcceptHandover(user.first, updatedSnapshot->getMecHostId()))
				{
					//lifecycleFile << received_packet->getTimeStamp() << ",HANDOVER_MEH," << user.second.getAddress() << "," << userIt->second.currentMEH << "->" << updatedSnapshot->getMecHostId() << endl;
				}
				else
				{
					EV << "SaveDataHistory - Handover NOT logged (lockout active for " << user.first << ")" << endl;
				}
			}
			*/
		}
	}

	controllerApp_->updateMehStateMap(received_packet);

    controllerApp_->updateUserStateMap(received_packet);

    // Periodic Flush
    msgCount_++;
    if (msgCount_ >= FLUSH_INTERVAL_) {
        userFile.flush();
        //lifecycleFile.flush();
        radioStatsFile.flush(); // Flush new file
        msgCount_ = 0;
    }

    return pck;
}

void SaveDataHistory::addUserUpdate(UserMEHUpdate& update)
{
	EV << "SaveDataHistory::addUserUpdate - user " << update.getAddress() <<
		" was sent to be added to the userUpdates map" << endl;

	// PERFORMANCE IMPROVEMENT: O(1) insert/update using map instead of O(n) linear search
	// Original linear search code commented out for reference:
	// for (auto& userUpdate : controllerApp_->userUpdates) {
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
		EV << "SaveDataHistory::addUserUpdate - user " << address << " added to the userUpdates map" << endl;
	} else {
		EV << "SaveDataHistory::addUserUpdate - user " << address << " was updated in the userUpdates map" << endl;
	}
}

SaveDataHistory::~SaveDataHistory()
{
    userFile.close();
    //lifecycleFile.close();
    radioStatsFile.close(); // Close new file
}
}
