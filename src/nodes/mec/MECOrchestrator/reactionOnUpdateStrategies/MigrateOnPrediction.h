#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_

#include "ReactionOnUpdate.h"
#include <unordered_map>
#include <string>

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
 * A predicted *departure* is recorded and not acted on. Being early buys nothing
 * there — the confirmed exit frees the same resources a few seconds later — while
 * being wrong is unrecoverable, since nothing recreates a deleted application and
 * the user would spend the rest of the run with nowhere to send its requests.
 * Predicted *moves* are the opposite case, and acting early on them is the whole
 * point of the strategy.
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
 *
 * Whether the handover acts at all is the reactiveFallback parameter, which is
 * what makes this class two comparable sub-profiles rather than one: with it the
 * run measures predictions backed by a safety net, without it the predictions
 * alone. Everything else is identical, including what RAVENS sends and what the
 * decision log records, so the difference between two such runs is the net.
 */
class MigrateOnPrediction : public ReactionOnUpdate
{
  private:
    omnetpp::cSimpleModule* owner_;   // MecOrchestrator, needed for scheduleAt/cancelAndDelete
    double migrationTime_;            // time it takes to complete a migration (to offset scheduling)

    // Whether a confirmed handover still moves the application when the model
    // missed it. See the parameter's own documentation in MecOrchestrator.ned;
    // in short, on measures predictions with a safety net under them and off
    // measures the predictions.
    bool reactiveFallback_;

    // A prediction waiting for its moment, and the facts about it that the
    // decision log needs when that moment comes. The self-message carries only
    // where to migrate; when it fires, the record still has to say which model
    // spoke, when the move was expected, and whether the prediction arrived in
    // time to act on — none of which survive in the message.
    struct ScheduledPrediction {
        omnetpp::cMessage* msg = nullptr;
        omnetpp::simtime_t observedAt = -1;
        omnetpp::simtime_t expectedAt = -1;
        std::string modelId;
        bool late = false;
    };

    // Map from UE address → the prediction scheduled for it
    std::unordered_map<std::string, ScheduledPrediction> scheduledPredictions_;

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

  public:
    MigrateOnPrediction(IOrchestratorApi* api, omnetpp::cSimpleModule* owner, double migrationTime,
                        bool reactiveFallback);
    virtual ~MigrateOnPrediction();

    virtual void reactOnUpdate(const simu5g::UserEvent&) override;
    virtual void reactOnUpdate(const std::vector<simu5g::MigrationPrediction>&) override;
    virtual void handleScheduledEvent(omnetpp::cMessage* msg) override;
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_ */
