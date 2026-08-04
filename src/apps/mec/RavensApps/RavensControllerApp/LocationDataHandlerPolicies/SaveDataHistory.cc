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
    // RadioTimestamp is when these aggregates were measured; TimestampSent is only
    // when the frame carrying them left. Without both, an aggregate that stopped
    // being refreshed is indistinguishable from a current one — the per-user rows
    // have always carried both, these rows did not.
    //
    // TimestampSent is named to match the same column in the users file: the two
    // get joined, and one quantity under two names is a trap for whoever joins them.
    radioStatsFile << "TimestampSent,RadioTimestamp,MEHId,CellId,DlPrbUsageCell,UlPrbUsageCell,DlNongbrPdrCell,UlNongbrPdrCell,"
                   << "AvgDlDelay,AvgUlDelay,TotalDlDataVolume,TotalUlDataVolume,NumActiveUeDlNongbr,AvgDistanceToAp" << endl;
    
    EV << "SaveDataHistory initialized. Users: " << name << ", Lifecycle: " << lifecycleName << ", RadioStats: " << radioStatsName << endl;
}

inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
    // Last-resort cleanup: drop users nothing has been heard about for far longer
    // than any normal gap, and record the departure. Departures are normally
    // confirmed through the event channel long before this, so this should stay
    // quiet — it exists so a user whose exit event was somehow never delivered
    // cannot linger indefinitely.
    std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
    for (const auto& user : removedUsers)
        onUserExit(user.userId, user.currentMEH, -1, SIMTIME_ZERO);

    // Log radio stats
    const AccessPointRadioInfoData& apRadioInfo = received_packet->getApRadioInfo();
    if (!apRadioInfo.getAccessPointId().empty()) {
        radioStatsFile << received_packet->getTimeStamp() << ","
                       << apRadioInfo.getTimestamp() << ","
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

    // Log per-user telemetry: one row per observation, not one per user. A frame
    // carries every Location Service reading taken since the previous frame, so a
    // user normally contributes several rows here — consecutive positions a
    // second apart rather than a single sample every few seconds.
    //
    // The columns are unchanged. What changes is that LocationTimestamp now
    // varies within a frame while TimestampSent does not, which makes
    // LocationTimestamp the meaningful key: it is when the observation was taken,
    // whereas TimestampSent is only when the frame happened to leave.
    //
    // Rows may include users the event channel never announced, and users that
    // had already left by the time the frame was sent. Both are intended: this
    // file records what was observed, while the lifecycle file records confirmed
    // placement. Filtering one against the other is an offline job.
    for (const auto& group : received_packet->getUserSamples()) {
        for (const auto& sample : group.samples) {
            const UserRadioInfoData& radioInfo = sample.getRadioInfo();
            userFile << received_packet->getTimeStamp() << ","
                     << sample.getTimestamp() << ","
                     << radioInfo.getTimestamp() << ","
                     << group.ueAddress << ","
                     << received_packet->getMecHostId() << ","
                     << sample.getAccessPointId() << ","
                     << radioInfo.getAccessPointId() << ","
                     << sample.getCurrentLocation().getX() << ","
                     << sample.getCurrentLocation().getY() << ","
                     << sample.getCurrentLocation().getZ() << ","
                     << sample.getCurrentLocation().getHorizontalSpeed() << ","
                     << sample.getCurrentLocation().getBearing() << ","
                     << sample.getDistanceToAP() << ","
                     << radioInfo.getDlNongbrDelayUe() << ","
                     << radioInfo.getUlNongbrDelayUe() << ","
                     << radioInfo.getDlNongbrPdrUe() << ","
                     << radioInfo.getUlNongbrPdrUe() << ","
                     << radioInfo.getDlNongbrDataVolumeUe() << ","
                     << radioInfo.getUlNongbrDataVolumeUe() << ","
                     << radioInfo.getRsrp() << "\n";
        }
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
