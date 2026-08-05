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
    // The header is generated from the same field list the rows are written
    // from, so a column cannot end up under the wrong name.
    std::string name = dirPath + "run_" + runNumber + "_users.csv";
    userFile.open(name, std::ios::out | std::ios::trunc);
    userFile << userSampleCsvHeader() << endl;
    
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
    // AvgDistanceToAp is deliberately absent. It was a Location Service average
    // living in a radio record, and it is recomputed offline from the per-user
    // rows: group users.csv by (LocationTimestamp, MEHId) and take the mean of
    // DistanceToAccessPoint. That is one value per second instead of one per
    // frame, and averaged over rows that share a timestamp.
    radioStatsFile << "TimestampSent,RadioTimestamp,MEHId,CellId,DlPrbUsageCell,UlPrbUsageCell,DlNongbrPdrCell,UlNongbrPdrCell,"
                   << "AvgDlDelay,AvgUlDelay,TotalDlDataVolume,TotalUlDataVolume,NumActiveUeDlNongbr" << endl;
    
    EV << "SaveDataHistory initialized. Users: " << name << ", Lifecycle: " << lifecycleName << ", RadioStats: " << radioStatsName << endl;
}

// One row per observation, not one per user. A frame carries every Location
// Service reading taken since the previous frame, so a user normally contributes
// several rows here — consecutive positions a second apart rather than a single
// sample every few seconds.
//
// LocationTimestamp varies within a frame while TimestampSent does not, which
// makes LocationTimestamp the meaningful key: it is when the observation was
// taken, whereas TimestampSent is only when the frame happened to leave.
//
// Rows may include users the event channel never announced, and users that had
// already left by the time the frame was sent. Both are intended: this file
// records what was observed, the lifecycle file records confirmed placement.
//
// Note which of the two may shape a model's input. Lifecycle is the right source
// for labels, which are facts about what happened after a sample and which the
// prediction server never has to reproduce. It is the wrong source for filtering
// features: the server receives telemetry only, so a training set pruned by a
// lifecycle join would carry a step the serving path cannot repeat. ConfirmedMEH
// is carried on both paths precisely so that test can be made per row,
// identically, in both places.
void SaveDataHistory::onUserSamples(const std::vector<UserSample>& samples)
{
    for (const auto& sample : samples) {
        bool firstColumn = true;
        sample.forEachField([this, &firstColumn](const char*, const auto& value) {
            if (!firstColumn)
                userFile << ',';
            firstColumn = false;
            userFile << value;
        });
        userFile << "\n";
    }
}

// Cell-level aggregates: one row per frame, not one per user. These describe the
// cell the Agent serves, so every user in the frame shares them.
void SaveDataHistory::onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
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
                       << apRadioInfo.getNumberOfActiveUeDlNongbrCell() << endl;
    }

    // Counted per frame, not per row: the two files are flushed together, and a
    // frame is the unit that produced both.
    msgCount_++;
    if (msgCount_ >= FLUSH_INTERVAL_) {
        userFile.flush();
        radioStatsFile.flush();
        msgCount_ = 0;
    }
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
