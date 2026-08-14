#include "CsvTelemetryRecorder.h"

namespace simu5g {

CsvTelemetryRecorder::CsvTelemetryRecorder(RavensControllerApp* controllerApp, std::string path)
    : TelemetrySink(controllerApp)
{
    std::string dirPath = ensureRunDirectory(path);
    std::string runNumber = std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber());

    // 1. User File (Standard Vectors + Radio Stats)
    // The header is generated from the same field list the rows are written
    // from, so a column cannot end up under the wrong name.
    std::string name = dirPath + "run_" + runNumber + "_users.csv";
    userFile.open(name, std::ios::out | std::ios::trunc);
    userFile << userSampleCsvHeader() << endl;

    // 2. Radio Stats File (DL/UL Usage and PDR)
    //
    // RadioTimestamp is when the reading was measured; TimestampSent only when the
    // frame carrying it left. TimestampSent is named to match the same column in
    // the users file: the two get joined, and one quantity under two names is a
    // trap for whoever joins them.
    //
    // AvgDistanceToAp is deliberately absent. It was a Location Service average
    // living in a radio record, and it is recomputed offline from the per-user
    // rows: group users.csv by (LocationTimestamp, MEHId) and take the mean of
    // DistanceToAccessPoint. That is one value per second instead of one per
    // frame, and averaged over rows that share a timestamp.
    std::string radioStatsName = dirPath + "run_" + runNumber + "_radio_stats.csv";
    radioStatsFile.open(radioStatsName, std::ios::out | std::ios::trunc);
    radioStatsFile << cellSampleCsvHeader() << endl;

    EV << "CsvTelemetryRecorder initialized. Users: " << name
       << ", RadioStats: " << radioStatsName << endl;
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
void CsvTelemetryRecorder::onUserSamples(const std::vector<UserSample>& samples)
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

// One row per RNIS reading, not one per frame. The RNIS reports once a second
// while frames leave less often, so a frame normally carries several — the same
// relationship the user rows have with the Location Service.
//
// Every reading gets a row, including one from a cell with no users. That cell
// is a state worth recording, not an absence of data: its delays read -1 and its
// volumes 0, which says "nobody here" rather than leaving the reader to guess
// from a missing row. An empty CellId would now mean something narrower and
// rarer — no stats collector on the cell at all — which is worth seeing too.
void CsvTelemetryRecorder::onCellSamples(const std::vector<CellSample>& samples)
{
    for (const auto& sample : samples) {
        bool firstColumn = true;
        sample.forEachField([this, &firstColumn](const char*, const auto& value) {
            if (!firstColumn)
                radioStatsFile << ',';
            firstColumn = false;
            radioStatsFile << value;
        });
        radioStatsFile << "\n";
    }
}

void CsvTelemetryRecorder::onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame)
{
    // Counted per frame, not per row: the two files are flushed together, and a
    // frame is the unit that produced both.
    msgCount_++;
    if (msgCount_ >= FLUSH_INTERVAL_) {
        userFile.flush();
        radioStatsFile.flush();
        msgCount_ = 0;
    }
}

CsvTelemetryRecorder::~CsvTelemetryRecorder()
{
    userFile.close();
    radioStatsFile.close();
}

} // namespace simu5g
