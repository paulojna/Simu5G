#ifndef RAVENS_CONTROLLER_APP_CSVEVENTRECORDER_H_
#define RAVENS_CONTROLLER_APP_CSVEVENTRECORDER_H_

#include "TelemetrySink.h"

#include <fstream>
#include <string>

namespace simu5g {

using namespace omnetpp;

// Writes every confirmed change in where a user is to lifecycle.csv — an account
// of what actually happened during a run.
//
// Separate from CsvTelemetryRecorder because the two record different kinds of
// thing and are wanted in different runs. Telemetry is what a model is trained
// on; this is what a run is judged by. A proactive experiment wants this file and
// has no use for a second copy of its training set, and the two were briefly a
// single switch that made that impossible to say.
//
// Costs nothing to leave on: a run produces a handful of these per user, against
// one telemetry row per user per second.
class CsvEventRecorder : public TelemetrySink
{
    protected:
        std::ofstream lifecycleFile;

        virtual void onUserEvent(const UserEvent& event, int samplesSinceChange) override;

    public:
        CsvEventRecorder(RavensControllerApp* controllerApp, std::string path);
        virtual ~CsvEventRecorder();
};

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_CSVEVENTRECORDER_H_ */
