#ifndef NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_
#define NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_


#include "nodes/mec/MECOrchestrator/interfaces/IOrchestrationApi.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/UserEvent.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/MigrationPrediction.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/TelemetrySample.h"

namespace simu5g {

class MecOrchestrator;

// How the orchestrator reacts to what RAVENS reports. One overload per stream,
// so a strategy is defined by which of them it implements rather than by a mode
// name — the three modes differ only in what they consume, which is what makes
// them comparable.
//
// Every overload has a default no-op body. A reactive strategy simply never
// overrides the prediction and telemetry ones; nothing about it needs to say so.
class ReactionOnUpdate
{
  protected:
    IOrchestratorApi* api_ = nullptr;

  public:
    explicit ReactionOnUpdate(IOrchestratorApi* api) : api_(api) {}

    // A confirmed change, delivered the moment the Controller concluded it.
    // Stays on in every mode: it is the only stream reporting what actually
    // happened, which makes it both the correction path for a wrong prediction
    // and the reward signal for a learning agent.
    virtual void reactOnUpdate(const UserEvent&) {}

    // Expected future changes.
    virtual void reactOnUpdate(const std::vector<MigrationPrediction>&) {}

    // One window of raw observations. Unused until a learning strategy exists;
    // declared here so that adding one is a new class rather than a change to
    // this boundary.
    virtual void reactOnTelemetry(omnetpp::simtime_t windowStart, omnetpp::simtime_t windowEnd,
                                  const std::vector<std::string>& reportingMEHIds,
                                  const std::vector<UserSample>& userSamples,
                                  const std::vector<CellSample>& cellSamples) {}

    virtual void handleScheduledEvent(omnetpp::cMessage*) {} // default no-op for strategies that don't schedule events
    virtual ~ReactionOnUpdate() {}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_ */
