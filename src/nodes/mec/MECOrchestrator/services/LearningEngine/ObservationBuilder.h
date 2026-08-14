#ifndef __OBSERVATIONBUILDER_H_
#define __OBSERVATIONBUILDER_H_

#include "nodes/mec/utils/httpUtils/json.hpp"
#include "nodes/mec/MECOrchestrator/interfaces/IOrchestrationApi.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/UserEvent.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/MigrationPrediction.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/TelemetrySample.h"

#include <string>
#include <vector>

namespace simu5g {

// One MEC host as the learning engine needs to know it. Sent once, at the start
// of the episode: none of it changes while a run is in progress.
struct HostTopology {
    std::string host;
    std::vector<std::string> cells;   // the cells this host's Location Service covers
    bool isCloud = false;             // the fallback host, reached when no edge host fits
    double maxRam = 0;
};

// One cell's position, so the engine can work out which host a user is heading
// towards. Sent rather than derived per step: positions are fixed for the whole
// run, and geometry belongs on the side that decides what to do with it.
struct CellTopology {
    std::string cell;
    double x = 0;
    double y = 0;
};

// What the strategy has accumulated since the previous window, as opposed to
// what the window itself carried.
//
// Events are read at the step boundary rather than acted on as they arrive:
// immediate delivery does not imply immediate consumption, and a user that moved
// twice between two windows has to appear twice rather than collapse into its
// latest position.
struct StepContext {
    std::vector<UserEvent> events;

    // Forwarded by the Controller, which is what calls the model server; the
    // orchestrator asks no model anything. Empty when the ablation is off,
    // because then the Controller derives that it must not call one at all.
    std::vector<MigrationPrediction> predictions;

    // What the engine asked for last step that could not be done, with the
    // reason. Reported back so a refused action is a signal rather than silence
    // — otherwise the engine cannot tell a rejected migration from one it never
    // requested.
    nlohmann::json rejectedActions = nlohmann::json::array();
};

// Turns the orchestrator's state into the payloads the learning engine reads.
//
// Separate from the strategy because the two change at different rates: the
// strategy's loop — window arrives, observe, decide, execute, record — is short
// and stable, while this is where every future addition to the observation
// lands. The same split TelemetrySample makes between the canonical records and
// the consumers that walk them.
//
// It sends facts, not features. Identity and absolute timestamps travel because
// actions come back addressed to a user, but nothing here decides what the
// engine's network is allowed to look at — that is the engine's own encoder,
// which is where it stays editable.
class ObservationBuilder {
  public:
    explicit ObservationBuilder(IOrchestratorApi* api) : api_(api) {}

    // The episode's fixed setup: the hosts, their cells, the cell positions, and
    // whatever the run was configured with. ramPerApp goes in the configuration
    // so the engine can derive each host's occupancy from the application view
    // it already receives, instead of being told a utilisation that is a fixed
    // multiple of a number it already has.
    nlohmann::json buildTopology(const std::vector<HostTopology>& hosts,
                                 const std::vector<CellTopology>& cells,
                                 const nlohmann::json& config) const;

    // One decision step. The first five arguments are the telemetry window
    // exactly as reactOnTelemetry receives it; the last is what the strategy has
    // been holding since the previous one.
    //
    // Carries no reward: the engine computes it from these same two views, which
    // keeps the penalty weights editable without rebuilding the simulation.
    nlohmann::json buildStep(omnetpp::simtime_t windowStart,
                             omnetpp::simtime_t windowEnd,
                             const std::vector<std::string>& reportingMEHIds,
                             const std::vector<UserSample>& userSamples,
                             const std::vector<CellSample>& cellSamples,
                             const StepContext& context) const;

  private:
    IOrchestratorApi* api_;

    // The two views, joined by user into one row each but kept as two nested
    // objects. Never flattened: a proactive migration puts the user on one host
    // and its application on another, and a single flat row invites a reader to
    // treat one as the other.
    nlohmann::json buildUsers() const;
};

} // namespace simu5g

#endif
