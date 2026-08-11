#ifndef RAVENS_CONTROLLER_APP_CSVTELEMETRYRECORDER_H_
#define RAVENS_CONTROLLER_APP_CSVTELEMETRYRECORDER_H_

#include "TelemetrySink.h"

#include <fstream>
#include <string>

namespace simu5g {

using namespace omnetpp;

// Writes the raw observations to users.csv and radio_stats.csv — the data models
// are trained on. Was SaveDataHistory, which also wrote the lifecycle file; that
// half is CsvEventRecorder now, because the two are wanted in different runs.
//
// This is the expensive recorder: one row per user per Location Service tick, so
// a run with forty users produces forty rows a second. Worth switching on to
// collect a training set, and worth switching off in an experiment that is going
// to be judged on its events.
class CsvTelemetryRecorder : public TelemetrySink
{
    protected:
        std::ofstream userFile;
        std::ofstream radioStatsFile;

        int msgCount_ = 0;
        static const int FLUSH_INTERVAL_ = 100;

        virtual void onUserSamples(const std::vector<UserSample>& samples) override;
        virtual void onCellSamples(const std::vector<CellSample>& samples) override;
        virtual void onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame) override;

    public:
        CsvTelemetryRecorder(RavensControllerApp* controllerApp, std::string path);
        virtual ~CsvTelemetryRecorder();
};

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_CSVTELEMETRYRECORDER_H_ */
