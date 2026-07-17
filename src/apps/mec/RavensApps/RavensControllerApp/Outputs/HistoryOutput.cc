#include "HistoryOutput.h"

namespace simu5g {

HistoryOutput::HistoryOutput(RavensControllerApp* controllerApp, std::string runDir)
    : RavensOutputBase(controllerApp)
{
    std::string runNumber = std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber());

    // 1. User File (Standard Vectors + Radio Stats)
    std::string name = runDir + "run_" + runNumber + "_users.csv";
    userFile.open(name, std::ios::out | std::ios::trunc);
    userFile << "TimestampSent,LocationTimestamp,RadioTimestamp,UEId,MEHId,AccessPointId,RNISCellId,"
             << "x,y,z,Speed,Bearing,DistanceToAccessPoint,"
             << "DlNongbrDelayUe,UlNongbrDelayUe,"
             << "DlNongbrPdrUe,UlNongbrPdrUe,"
             << "DlNongbrDataVolumeUe,UlNongbrDataVolumeUe,Rsrp" << endl;

    // 2. Lifecycle File (Events)
    std::string lifecycleName = runDir + "run_" + runNumber + "_lifecycle.csv";
    lifecycleFile.open(lifecycleName, std::ios::out | std::ios::trunc);
    lifecycleFile << "timestamp,eventType,userId,fromMEH,toMEH,samplesSinceChange,firstDetectedAt" << endl;

    // 3. Radio Stats File (DL/UL Usage and PDR)
    std::string radioStatsName = runDir + "run_" + runNumber + "_radio_stats.csv";
    radioStatsFile.open(radioStatsName, std::ios::out | std::ios::trunc);
    radioStatsFile << "Timestamp,MEHId,CellId,DlPrbUsageCell,UlPrbUsageCell,DlNongbrPdrCell,UlNongbrPdrCell,"
                   << "AvgDlDelay,AvgUlDelay,TotalDlDataVolume,TotalUlDataVolume,NumActiveUeDlNongbr,AvgDistanceToAp" << endl;

    EV << "HistoryOutput initialized. Users: " << name << ", Lifecycle: " << lifecycleName << ", RadioStats: " << radioStatsName << endl;
}

void HistoryOutput::onTelemetry(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
{
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
}

void HistoryOutput::onUserEntry(const std::string& userId, const std::string& meh,
                                int samplesSinceChange, omnetpp::simtime_t firstDetectedAt)
{
    lifecycleFile << simTime() << ",ENTRY," << userId << ",," << meh << ","
                  << samplesSinceChange << "," << firstDetectedAt << "\n";
    lifecycleFile.flush();
}

void HistoryOutput::onUserHandover(const std::string& userId,
                                   const std::string& fromMeh, const std::string& toMeh,
                                   int samplesSinceChange, omnetpp::simtime_t firstDetectedAt)
{
    lifecycleFile << simTime() << ",HANDOVER," << userId << "," << fromMeh << "," << toMeh << ","
                  << samplesSinceChange << "," << firstDetectedAt << "\n";
    lifecycleFile.flush();
}

void HistoryOutput::onUserExit(const std::string& userId, const std::string& fromMeh,
                               int samplesSinceChange, omnetpp::simtime_t firstDetectedAt)
{
    lifecycleFile << simTime() << ",EXIT," << userId << "," << fromMeh << ",,"
                  << samplesSinceChange << "," << firstDetectedAt << "\n";
    lifecycleFile.flush();
}

HistoryOutput::~HistoryOutput()
{
    userFile.close();
    lifecycleFile.close();
    radioStatsFile.close();
}
}
