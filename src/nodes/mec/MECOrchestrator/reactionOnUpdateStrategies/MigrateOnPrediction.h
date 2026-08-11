#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_

#include "ReactionOnUpdate.h"
#include <unordered_map>
#include <fstream>

namespace simu5g {

/**
 * MigrateOnPrediction Strategy
 *
 * The proactive mode: migrates an application before the user gets there, from
 * predictions produced by a model outside the simulation.
 *
 * Consumes two streams.
 *
 * Predictions are the point of the strategy. Each one is scheduled as a
 * self-message timed so the migration *completes* at the predicted moment rather
 * than starting then. A newer prediction for the same user replaces the older
 * one only if it names a different destination; agreeing predictions leave the
 * earlier one in place, since it has the better lead time.
 *
 * Events are the correction path, and stay on for that reason:
 *   EXIT     -> remove the app, and cancel any prediction still pending for it
 *   ENTRY    -> ask whether a migration is needed
 *   HANDOVER -> a move the model did not predict, or predicted too late. The
 *               model needs roughly half a minute of observations before it can
 *               say anything at all, so every run has a warmup during which
 *               this is the only thing moving applications.
 *
 * A handover deliberately does *not* cancel a pending prediction. Letting both
 * run is what makes the reactive fallback measurable against the proactive path
 * instead of quietly substituting for it; checkIfMigrationIsNeeded is idempotent,
 * so the loser of the race finds the app already where it wanted it.
 */
class MigrateOnPrediction : public ReactionOnUpdate
{
  private:
    omnetpp::cSimpleModule* owner_;   // MecOrchestrator, needed for scheduleAt/cancelAndDelete
    double migrationTime_;            // time it takes to complete a migration (to offset scheduling)

    // Map from UE address → scheduled self-message pointer
    std::unordered_map<std::string, omnetpp::cMessage*> scheduledPredictions_;

    // Predictions that arrived with less lead time than a migration takes, and
    // so had to start immediately instead of at the right moment.
    //
    // This is the direct measurement of whether the model predicts far enough
    // ahead. For a proactive migration to be in place before the user arrives,
    // the model's horizon has to cover the telemetry interval, the model's own
    // inference time, the hop to here, and the migration itself — about 13.5
    // seconds with the current settings. Every count here is one prediction that
    // did not, and a run where this equals the number of predictions is a run
    // that was proactive in name only.
    long latePredictions_ = 0;

    // CSV log file for post-simulation analysis of migration types
    std::ofstream logFile_;

  public:
    MigrateOnPrediction(IOrchestratorApi* api, omnetpp::cSimpleModule* owner, double migrationTime);
    virtual ~MigrateOnPrediction();

    virtual void reactOnUpdate(const simu5g::UserEvent&) override;
    virtual void reactOnUpdate(const std::vector<simu5g::MigrationPrediction>&) override;
    virtual void handleScheduledEvent(omnetpp::cMessage* msg) override;
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_ */
