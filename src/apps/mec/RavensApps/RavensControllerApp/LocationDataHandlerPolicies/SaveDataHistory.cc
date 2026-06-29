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
    lifecycleFile.open(lifecycleName, std::ios::out | std::ios::trunc);
    lifecycleFile << "timestamp,eventType,userId,fromMEH,toMEH,samplesSinceChange,firstDetectedAt" << endl;

    // 3. Radio Stats File (DL/UL Usage and PDR)
    std::string radioStatsName = dirPath + "run_" + runNumber + "_radio_stats.csv";
    radioStatsFile.open(radioStatsName, std::ios::out | std::ios::trunc);
    radioStatsFile << "Timestamp,MEHId,CellId,DlPrbUsageCell,UlPrbUsageCell,DlNongbrPdrCell,UlNongbrPdrCell" << endl;
    
    EV << "SaveDataHistory initialized. Users: " << name << ", Lifecycle: " << lifecycleName << ", RadioStats: " << radioStatsName << endl;
}

inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
    inet::Packet* pck = nullptr;

    // C1 safety net: purge users absent for longer than threshold_
    std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
    for (const auto& user : removedUsers) {
        UserMEHUpdate update;
        update.setLastMEHId(user.currentMEH);
        update.setNewMEHId("");
        update.setAddress(user.userId);
        addUserUpdate(update);
    }

    // Log radio stats
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

    // Log per-user telemetry
    for (const auto& [address, userData] : received_packet->getUsers()) {
        userFile << received_packet->getTimeStamp() << ","
                 << userData.getLastUpdated() << ","
                 << userData.getLsUpdate() << ","
                 << userData.getRnisUpdate() << ","
                 << address << ","
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

    // Entry/exit detection moved to handleEventFrame() (eRAVENS R1)
    // State map updates done in socketDataArrived() before policy is called

    msgCount_++;
    if (msgCount_ >= FLUSH_INTERVAL_) {
        userFile.flush();
        radioStatsFile.flush();
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

void SaveDataHistory::handleEventMessage(const RavensEventList& events, const std::string& sourceMEH)
{
    for (const auto& e : events) {
        std::string eventType;
        std::string fromMEH;
        std::string toMEH;

        if (e.eventType == EVENT_ENTRY) {
            toMEH = sourceMEH;
            auto userIt = controllerApp_->userStateMap.find(e.ueAddress);
            if (userIt != controllerApp_->userStateMap.end() && !userIt->second.currentMEH.empty()) {
                fromMEH = userIt->second.currentMEH;
                // pendingExitTime != 0 means user was in exit hold — this is a HANDOVER
                eventType = (userIt->second.pendingExitTime != 0) ? "HANDOVER" : "ENTRY";
            } else {
                eventType = "ENTRY";
            }
        } else {
            eventType = "EXIT";
            fromMEH = sourceMEH;
        }

        lifecycleFile << simTime() << ","
                      << eventType << ","
                      << e.ueAddress << ","
                      << fromMEH << ","
                      << toMEH << ","
                      << e.samplesSinceChange << ","
                      << e.firstDetectedAt << "\n";
    }
    lifecycleFile.flush();
}

SaveDataHistory::~SaveDataHistory()
{
    userFile.close();
    lifecycleFile.close();
    radioStatsFile.close();
}
}
