#ifndef RAVENS_CONTROLLER_APP_TELEMETRYSINK_H_
#define RAVENS_CONTROLLER_APP_TELEMETRYSINK_H_

#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/TelemetrySample.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/UserEvent.h"
#include "apps/mec/RavensApps/RavensLinkPacket_m.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <string>
#include <vector>

namespace simu5g {

class RavensControllerApp;

// The directory this run's CSV files go in, created if it is not there. Shared
// because more than one recorder writes into it and they must agree on where it
// is; the run number keeps successive runs from overwriting each other.
inline std::string ensureRunDirectory(const std::string& path)
{
    std::string runNumber = std::to_string(omnetpp::getEnvir()->getConfigEx()->getActiveRunNumber());
    std::string dirPath = path + "run_" + runNumber + "/";

    // Read/write/search for owner and group, read/search for everyone else.
    // Already existing is the normal case on a re-run, not an error.
    if (mkdir(dirPath.c_str(), 0775) == -1 && errno != EEXIST)
        EV << "ensureRunDirectory - could not create " << dirPath << ": " << strerror(errno) << endl;

    return dirPath;
}

// Somewhere the Controller's observations go, besides the orchestrator.
//
// These were the "location data handler policies", of which exactly one could be
// active. That was the problem: recording a run to file and driving migrations
// from a model are unrelated activities, and being forced to pick one meant the
// most useful configuration — record the run *and* let the model act — could not
// be expressed at all. A run now holds a list of these, and every one of them
// sees everything.
//
// Note what is no longer here. Every policy also published placement changes to
// the orchestrator, in three near-identical copies of the same two lines. That
// was never a policy's decision to make: the orchestrator hears about a
// confirmed event because it is confirmed, not because of what some file is
// being written. The Controller publishes events itself now, and a sink that
// wants to know about one overrides onUserEvent.
//
// Every hook is a no-op by default; a sink overrides only what it needs.
class TelemetrySink
{
    protected:
        RavensControllerApp* controllerApp_;

        // One user's observations from the telemetry frame being processed,
        // oldest first. Called once per user in the frame.
        //
        // A frame carries one observation per user, so this currently always
        // holds a single element. It stays a sequence because a per-user
        // sequence is what consumers want, and because the frame interval is a
        // parameter — one observation per call is a value of it, not a property
        // of the interface.
        virtual void onUserSamples(const std::vector<UserSample>& samples) {}

        // The cell readings the frame carries, or nothing before the RNIS has
        // first replied. Called once per frame, with every reading in it.
        virtual void onCellSamples(const std::vector<CellSample>& samples) {}

        // Called once per telemetry frame, after every sample and every cell
        // reading in it has been delivered. Marks the end of the frame: it is
        // where a sink that ships the whole frame in one request sends it.
        virtual void onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame) {}

        // A confirmed change in where a user is, at the moment the Controller
        // concludes it — the same event, and the same instant, as the one going
        // to the orchestrator.
        //
        // This was three hooks, one per event type, which every implementation
        // then had to reunite into one row. samplesSinceChange stays a separate
        // argument because it is the Agent's evidence for the event rather than
        // part of the event: how many consecutive observations it took to become
        // sure. Useful for judging the detector, meaningless to anyone acting on
        // the result, and so deliberately not carried in UserEvent itself.
        virtual void onUserEvent(const UserEvent& event, int samplesSinceChange) {}

        friend class RavensControllerApp;

    public:
        TelemetrySink(RavensControllerApp* controllerApp) : controllerApp_(controllerApp) {}
        virtual ~TelemetrySink() {}
};

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_TELEMETRYSINK_H_ */
