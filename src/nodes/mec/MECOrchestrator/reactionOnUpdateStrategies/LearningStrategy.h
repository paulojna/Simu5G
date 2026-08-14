#ifndef NODES_MEC_MECORCHESTRATOR_LEARNINGSTRATEGY_H_
#define NODES_MEC_MECORCHESTRATOR_LEARNINGSTRATEGY_H_

#include "ReactionOnUpdate.h"
#include "nodes/mec/MECOrchestrator/services/LearningEngine/LearningEngineClient.h"
#include "nodes/mec/MECOrchestrator/services/LearningEngine/ObservationBuilder.h"

namespace simu5g {

/**
 * LearningStrategy
 *
 * The mode whose policy is not in this file. Each telemetry window it assembles
 * what the orchestrator knows, sends it to a reinforcement-learning engine
 * outside the simulation, and carries out whatever comes back — through the same
 * IOrchestratorApi calls the other strategies use, so the decision log and the
 * lifecycle rules apply unchanged.
 *
 * The decision step is the telemetry window. Events arriving between windows are
 * held and read as part of the next step rather than acted on one by one:
 * immediate delivery does not imply immediate consumption, and a user that moved
 * twice inside a window has to reach the engine as two moves.
 *
 * One exception, and it is deliberate. A confirmed EXIT removes the application
 * immediately, exactly as RemoveOnExit and the two migrating strategies do. It
 * is not an action the engine may take or withhold: deletion on exit is a rule
 * of the system, identical in every arm, and letting the agent decide it would
 * make the learning arm play a different game from the baselines it is measured
 * against. The engine decides *where* applications live, never *whether*.
 *
 * The action set is therefore one verb: migrate a named user to a named host.
 * Waiting is the absence of an action. Timing is not a separate verb either —
 * acting on the window before a handover is a proactive migration and acting on
 * the window after it is a reactive one, so *when* is expressed by which step
 * the engine chooses to act on.
 *
 * No reward is computed here. Both halves of it — users whose application is not
 * on the host observing them, and the cost of each action — are derivable from
 * the observation the engine already receives, and leaving the arithmetic on
 * that side keeps the penalty weights editable without rebuilding the
 * simulation.
 */
class LearningStrategy : public ReactionOnUpdate
{
  private:
    LearningEngineClient client_;
    ObservationBuilder builder_;

    // What has arrived since the previous window, drained into each step.
    StepContext context_;

    // The end of the window this step is deciding on: the instant beyond which
    // its inputs know nothing. Recorded on every decision so the log measures
    // how stale the engine's view was, the same way the event-driven strategies
    // record the observedAt of the event they acted on.
    omnetpp::simtime_t lastWindowEnd_ = -1;

    // Runs what the engine asked for and returns what could not be done, with
    // reasons. Refusals are recorded as decisions and handed back in the next
    // observation: an action that silently vanishes is indistinguishable to the
    // engine from one it never sent.
    nlohmann::json executeActions(const nlohmann::json& actions);

  public:
    LearningStrategy(IOrchestratorApi* api, const std::string& engineBaseUrl,
                     double engineTimeout);

    // Called once the module tree is fully initialised — later than this object
    // is built, because cell positions are not readable at INITSTAGE_LOCAL.
    void openEpisode(const std::vector<HostTopology>& hosts,
                     const std::vector<CellTopology>& cells,
                     const nlohmann::json& config);

    // End of run: the terminal marker, so the engine can close out the last
    // transition instead of waiting for a step that never comes.
    void closeEpisode();

    virtual void reactOnUpdate(const UserEvent&) override;
    virtual void reactOnUpdate(const std::vector<MigrationPrediction>&) override;
    virtual void reactOnTelemetry(omnetpp::simtime_t windowStart, omnetpp::simtime_t windowEnd,
                                  const std::vector<std::string>& reportingMEHIds,
                                  const std::vector<UserSample>& userSamples,
                                  const std::vector<CellSample>& cellSamples) override;
    virtual ~LearningStrategy() {}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_LEARNINGSTRATEGY_H_ */
