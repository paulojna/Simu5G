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
    userFile << "TimestampSent,LocationTimestamp,RadioTimestamp,UEId,MEHId,AccessPointId,RNISCellId,"
             << "x,y,z,Speed,Bearing,DistanceToAccessPoint,"
             << "DlNongbrDelayUe,UlNongbrDelayUe,"
             << "DlNongbrPdrUe,UlNongbrPdrUe,"
             << "DlNongbrDataVolumeUe,UlNongbrDataVolumeUe,Rsrp" << endl;
    
    // 2. Lifecycle File (Events)
    std::string lifecycleName = dirPath + "run_" + runNumber + "_lifecycle.csv";
    lifecycleFile.open(lifecycleName, std::ios::out | std::ios::trunc);
    lifecycleFile << "timestamp,eventType,userId,fromMEH,toMEH,samplesSinceChange,firstDetectedAt" << endl;

    // 3. Radio Stats File (DL/UL Usage and PDR)
    std::string radioStatsName = dirPath + "run_" + runNumber + "_radio_stats.csv";
    radioStatsFile.open(radioStatsName, std::ios::out | std::ios::trunc);
    radioStatsFile << "Timestamp,MEHId,CellId,DlPrbUsageCell,UlPrbUsageCell,DlNongbrPdrCell,UlNongbrPdrCell,"
                   << "AvgDlDelay,AvgUlDelay,TotalDlDataVolume,TotalUlDataVolume,NumActiveUeDlNongbr,AvgDistanceToAp" << endl;
    
    EV << "SaveDataHistory initialized. Users: " << name << ", Lifecycle: " << lifecycleName << ", RadioStats: " << radioStatsName << endl;
}

inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
    // C1 safety net: purge users absent longer than threshold_ — log as EXIT, MEO-silent
    std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
    for (const auto& user : removedUsers)
        onUserExit(user.userId, user.currentMEH, -1, SIMTIME_ZERO);

    // Log radio stats
    const AccessPointRadioInfoData& apRadioInfo = received_packet->getApRadioInfo();
    if (!apRadioInfo.getAccessPointId().empty()) {
        radioStatsFile << received_packet->getTimeStamp() << ","
                       << received_packet->getMecHostId() << ","
                       << apRadioInfo.getAccessPointId() << ","
                       << apRadioInfo.getDlTotalPrbUsageCell() << ","
                       << apRadioInfo.getUlTotalPrbUsageCell() << ","
                       << apRadioInfo.getDlNongbrPdrCell() << ","
                       << apRadioInfo.getUlNongbrPdrCell() << ","
                       << apRadioInfo.getAvgDlDelay() << ","
                       << apRadioInfo.getAvgUlDelay() << ","
                       << apRadioInfo.getTotalDlDataVolume() << ","
                       << apRadioInfo.getTotalUlDataVolume() << ","
                       << apRadioInfo.getNumberOfActiveUeDlNongbrCell() << ","
                       << apRadioInfo.getAvgDistanceToAp() << endl;
    }

    // Log per-user telemetry
    for (const auto& [address, userData] : received_packet->getUsers()) {
        const UserRadioInfoData& radioInfo = userData.getRadioInfo();
        userFile << received_packet->getTimeStamp() << ","
                 << userData.getTimestamp() << ","
                 << radioInfo.getTimestamp() << ","
                 << address << ","
                 << received_packet->getMecHostId() << ","
                 << userData.getAccessPointId() << ","
                 << radioInfo.getAccessPointId() << ","
                 << userData.getCurrentLocation().getX() << ","
                 << userData.getCurrentLocation().getY() << ","
                 << userData.getCurrentLocation().getZ() << ","
                 << userData.getCurrentLocation().getHorizontalSpeed() << ","
                 << userData.getCurrentLocation().getBearing() << ","
                 << userData.getDistanceToAP() << ","
                 << radioInfo.getDlNongbrDelayUe() << ","
                 << radioInfo.getUlNongbrDelayUe() << ","
                 << radioInfo.getDlNongbrPdrUe() << ","
                 << radioInfo.getUlNongbrPdrUe() << ","
                 << radioInfo.getDlNongbrDataVolumeUe() << ","
                 << radioInfo.getUlNongbrDataVolumeUe() << ","
                 << radioInfo.getRsrp() << "\n";
    }

    msgCount_++;
    if (msgCount_ >= FLUSH_INTERVAL_) {
        userFile.flush();
        radioStatsFile.flush();
        msgCount_ = 0;
    }

    return nullptr;
}

void SaveDataHistory::onUserEntry(const std::string& userId, const std::string& meh,
                                  int samplesSinceChange, omnetpp::simtime_t firstDetectedAt)
{
    lifecycleFile << simTime() << ",ENTRY," << userId << ",," << meh << ","
                  << samplesSinceChange << "," << firstDetectedAt << "\n";
    lifecycleFile.flush();
}

void SaveDataHistory::onUserHandover(const std::string& userId,
                                     const std::string& fromMeh, const std::string& toMeh,
                                     int samplesSinceChange, omnetpp::simtime_t firstDetectedAt)
{
    lifecycleFile << simTime() << ",HANDOVER," << userId << "," << fromMeh << "," << toMeh << ","
                  << samplesSinceChange << "," << firstDetectedAt << "\n";
    lifecycleFile.flush();
}

void SaveDataHistory::onUserExit(const std::string& userId, const std::string& fromMeh,
                                 int samplesSinceChange, omnetpp::simtime_t firstDetectedAt)
{
    lifecycleFile << simTime() << ",EXIT," << userId << "," << fromMeh << ",,"
                  << samplesSinceChange << "," << firstDetectedAt << "\n";
    lifecycleFile.flush();
}

SaveDataHistory::~SaveDataHistory()
{
    userFile.close();
    lifecycleFile.close();
    radioStatsFile.close();
}
}
