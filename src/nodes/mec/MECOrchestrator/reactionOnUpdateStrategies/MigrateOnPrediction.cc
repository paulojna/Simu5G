#include "MigrateOnPrediction.h"
#include "DecisionRecording.h"
#include "nodes/mec/MECOrchestrator/services/MecAppMigrationManager/MecAppMigrationManager.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"

/*
 * MigrateOnPrediction Strategy
 *
 * Proactive migration from model predictions. See header for full documentation.
 *
 * Self-message lifecycle:
 *   - Created in reactOnUpdate(vector<MigrationPrediction>)
 *   - Tracked in scheduledPredictions_ map (keyed by UE address)
 *   - Cancelled: cancelAndDelete + erase (on new prediction for same UE, or exit)
 *   - Fired: erase from map + execute migration (message deleted by MecOrchestrator::handleMessage)
 *   - Shutdown: destructor cancels all outstanding messages
 */

namespace simu5g {

using namespace omnetpp;

MigrateOnPrediction::MigrateOnPrediction(IOrchestratorApi* api, cSimpleModule* owner, double migrationTime)
    : ReactionOnUpdate(api), owner_(owner), migrationTime_(migrationTime)
{
    // Decisions go to the orchestrator's decision log, which every strategy
    // shares. This used to keep a private CSV of its own in the working
    // directory, which put the proactive run's record in a different place and
    // a different shape from every other run's.
}

MigrateOnPrediction::~MigrateOnPrediction()
{
    // Cancel all outstanding scheduled predictions to prevent simulation cleanup errors
    for (auto& [address, scheduled] : scheduledPredictions_) {
        owner_->cancelAndDelete(scheduled.msg);
    }
    scheduledPredictions_.clear();

    // Also visible per decision in the log: a row whose reason says the
    // prediction was late. This total is the quick read of the same fact.
    std::cout << "[MigrateOnPrediction] predictions that arrived too late to schedule properly: "
              << latePredictions_ << std::endl;
}

// ─── Correction path: confirmed events ───

void MigrateOnPrediction::reactOnUpdate(const UserEvent& event)
{
    switch (event.eventType) {

    case USER_EXIT: {
        EV << "MigrateOnPrediction::reactOnUpdate - UE " << event.ueAddress << " exited system" << endl;

        // Cancel any scheduled prediction for this user (only exits cancel!)
        auto it = scheduledPredictions_.find(event.ueAddress);
        if (it != scheduledPredictions_.end())
        {
            EV << "MigrateOnPrediction::reactOnUpdate - Cancelling scheduled prediction for exiting UE "
               << event.ueAddress << endl;
            owner_->cancelAndDelete(it->second.msg);
            scheduledPredictions_.erase(it);
        }

        // Asked before the removal: afterwards there is no telling a user whose
        // application was deleted from one that never had one.
        bool hadApp = !api_->getAppCurrentMEH(event.ueAddress).empty();

        api_->removeAppFromSystem(event.ueAddress, event.fromMEHId);

        OrchestrationDecision decision = decisionFromEvent(event);
        decision.kind = hadApp ? DecisionKind::Remove : DecisionKind::None;
        decision.outcome = hadApp ? DecisionOutcome::Success : DecisionOutcome::NotNeeded;
        if (!hadApp)
            decision.reason = "no application to remove";
        api_->recordDecision(decision);
        break;
    }

    case USER_ENTRY: {
        EV << "MigrateOnPrediction::reactOnUpdate - New UE " << event.ueAddress
           << " detected at " << event.toMEHId << endl;

        MigrationResult result = api_->checkIfMigrationIsNeeded(
            event.ueAddress, event.toMEHId, event.fromMEHId);

        if (result.success) {
            EV << "MigrateOnPrediction::reactOnUpdate - Migration initiated for new UE, request #"
               << result.requestNumber << endl;
        } else {
            EV << "MigrateOnPrediction::reactOnUpdate - " << result.errorMessage << endl;
        }

        OrchestrationDecision decision = decisionFromEvent(event);
        fillFromMigrationResult(decision, result);
        api_->recordDecision(decision);
        break;
    }

    case USER_HANDOVER: {
        // A move the model did not predict, or predicted too late to act on.
        // checkIfMigrationIsNeeded is idempotent: if the app was already moved
        // proactively, this finds it in the right place and reports that nothing
        // was needed.
        EV << "MigrateOnPrediction::reactOnUpdate - Reactive fallback: handover detected" << endl;
        EV << "  UE: " << event.ueAddress << endl;
        EV << "  From: " << event.fromMEHId << " To: " << event.toMEHId << endl;

        MigrationResult result = api_->checkIfMigrationIsNeeded(
            event.ueAddress, event.toMEHId, event.fromMEHId);

        if (result.success) {
            EV << "MigrateOnPrediction::reactOnUpdate - Reactive migration initiated, request #"
               << result.requestNumber << endl;
            std::cout << "[MigrateOnPrediction t=" << simTime()
                      << "] REACTIVE fallback migration for UE " << event.ueAddress
                      << " from " << event.fromMEHId << " to " << event.toMEHId << std::endl;
        } else {
            // Expected when the model already handled this proactively
            EV << "MigrateOnPrediction::reactOnUpdate - " << result.errorMessage << endl;
        }

        // Recorded with trigger ConfirmedEvent, which is what separates the
        // reactive fallback from the proactive path in the same run: rows the
        // model produced carry trigger Prediction.
        OrchestrationDecision decision = decisionFromEvent(event);
        fillFromMigrationResult(decision, result);
        api_->recordDecision(decision);
        break;
    }

    default:
        EV << "MigrateOnPrediction::reactOnUpdate - unknown event type " << event.eventType
           << " for UE " << event.ueAddress << ", ignoring" << endl;
        break;
    }
}

// ─── Proactive path: schedule migrations from predictions ───

void MigrateOnPrediction::reactOnUpdate(const std::vector<MigrationPrediction>& predictions)
{
    EV << "MigrateOnPrediction::reactOnUpdate - Processing " << predictions.size() << " predictions" << endl;

    for (const auto& pred : predictions)
    {
        const std::string& ueAddress = pred.ueAddress;

        // Check for existing scheduled prediction for this UE
        auto it = scheduledPredictions_.find(ueAddress);
        if (it != scheduledPredictions_.end())
        {
            MigrateAppMessage* existingMsg = check_and_cast<MigrateAppMessage*>(it->second.msg);
            std::string existingTarget = existingMsg->getNewMEHId();

            if (existingTarget == pred.toMEHId)
            {
                // Same target — keep existing prediction (better lead time)
                EV << "MigrateOnPrediction - Keeping existing prediction for UE " << ueAddress
                   << " (same target " << existingTarget << ")" << endl;
                continue;
            }

            // Different target — replace with new prediction
            EV << "MigrateOnPrediction - Replacing prediction for UE " << ueAddress
               << " (target changed: " << existingTarget << " -> " << pred.toMEHId << ")" << endl;
            owner_->cancelAndDelete(it->second.msg);
            scheduledPredictions_.erase(it);
        }

        // Start the migration early enough that it *completes* when the user is
        // expected to arrive, rather than starting then.
        simtime_t adjustedDelay = pred.expectedAt - simTime() - migrationTime_;

        // A negative delay means the prediction did not leave enough time to act
        // on it — the horizon was eaten by the pipeline, or the model simply did
        // not look far enough ahead. Starting immediately is the best that can be
        // done, but it is no longer proactive, so it is counted rather than
        // quietly clamped.
        bool late = false;
        if (adjustedDelay < 0) {
            late = true;
            latePredictions_++;
            EV_WARN << "MigrateOnPrediction - prediction for " << ueAddress << " arrived "
                    << -adjustedDelay << "s too late to complete on time, starting now" << endl;
            adjustedDelay = 0;
        }

        // Create self-message using existing MigrateAppMessage
        MigrateAppMessage* msg = new MigrateAppMessage("ScheduledMigration");
        msg->setUeAddress(ueAddress.c_str());
        msg->setNewMEHId(pred.toMEHId.c_str());
        msg->setOldMEHId(pred.fromMEHId.c_str());
        msg->setType(pred.isExitPrediction() ? MIGRATE_APP_EXIT : MIGRATE_APP_MOVE);

        // Schedule and track, keeping the facts the record will need when it fires
        owner_->scheduleAt(simTime() + adjustedDelay, msg);

        ScheduledPrediction scheduled;
        scheduled.msg = msg;
        scheduled.observedAt = pred.observedAt;
        scheduled.expectedAt = pred.expectedAt;
        scheduled.modelId = pred.modelId;
        scheduled.late = late;
        scheduledPredictions_[ueAddress] = scheduled;

        EV << "MigrateOnPrediction - Scheduled "
           << (pred.isExitPrediction() ? "EXIT" : "MIGRATION")
           << " for UE " << ueAddress
           << " | target=" << pred.toMEHId
           << " | expected at " << pred.expectedAt
           << " | starts in " << adjustedDelay << "s" << endl;
    }
}

// ─── Self-message handler: execute scheduled migration/exit ───

void MigrateOnPrediction::handleScheduledEvent(cMessage* msg)
{
    MigrateAppMessage* migrateMsg = check_and_cast<MigrateAppMessage*>(msg);

    std::string ueAddress = migrateMsg->getUeAddress();
    std::string newMEHId  = migrateMsg->getNewMEHId();
    std::string oldMEHId  = migrateMsg->getOldMEHId();
    int type = migrateMsg->getType();

    // Take the prediction's own facts before dropping the entry: the record has
    // to say which model spoke and when the move was expected, and neither of
    // those is carried in the message.
    // (message itself will be deleted by MecOrchestrator::handleMessage after this returns)
    ScheduledPrediction scheduled;
    auto it = scheduledPredictions_.find(ueAddress);
    if (it != scheduledPredictions_.end()) {
        scheduled = it->second;
        scheduledPredictions_.erase(it);
    }

    OrchestrationDecision decision;
    decision.decidedAt = simTime();
    decision.ueAddress = canonicalUeAddress(ueAddress);
    decision.trigger = DecisionTrigger::Prediction;
    decision.observedAt = scheduled.observedAt;
    decision.expectedAt = scheduled.expectedAt;
    decision.modelId = scheduled.modelId;
    decision.fromMEHId = oldMEHId;
    decision.toMEHId = newMEHId;

    if (type == MIGRATE_APP_EXIT)
    {
        // Recorded, never executed — decided in item 2 of meo-plan.md. Acting on
        // a predicted exit deletes the application of a user that may not leave,
        // and nothing recreates it: the user is dark for the rest of the run, in
        // this arm alone. Waiting costs nothing measurable, since the confirmed
        // exit frees the same resources seconds later in scenarios where
        // capacity never binds.
        EV << "MigrateOnPrediction::handleScheduledEvent - Predicted EXIT for UE "
           << ueAddress << " recorded, not executed" << endl;

        decision.kind = DecisionKind::None;
        decision.outcome = DecisionOutcome::NotNeeded;
        decision.reason = "predicted exit not executed; removal waits for the confirmed exit";
        api_->recordDecision(decision);
        return;
    }

    // Predicted migration — verify app is still at expected source MEH
    std::string currentMEH = api_->getAppCurrentMEH(ueAddress);
    if (!currentMEH.empty() && currentMEH != oldMEHId)
    {
        EV << "MigrateOnPrediction::handleScheduledEvent - Stale prediction for UE "
           << ueAddress << " (expected " << oldMEHId << ", app at " << currentMEH << ")" << endl;

        decision.kind = DecisionKind::None;
        decision.outcome = DecisionOutcome::NotNeeded;
        decision.reason = "stale prediction: application is on " + currentMEH;
        api_->recordDecision(decision);
        return;
    }

    EV << "MigrateOnPrediction::handleScheduledEvent - Executing predicted MIGRATION for UE "
       << ueAddress << " from " << oldMEHId << " to " << newMEHId << endl;

    MigrationResult result = api_->migrateApp(ueAddress, newMEHId, oldMEHId);

    if (!result.success) {
        EV << "MigrateOnPrediction::handleScheduledEvent - Migration failed: "
           << result.errorMessage << endl;
    } else {
        EV << "MigrateOnPrediction::handleScheduledEvent - Migration initiated, request #"
           << result.requestNumber << endl;
    }

    fillFromMigrationResult(decision, result);

    // Whether this one started on time or was already behind when it arrived.
    // decidedAt against expectedAt gives the rest of the arithmetic offline.
    if (scheduled.late) {
        decision.reason = decision.reason.empty()
            ? std::string("prediction arrived too late to complete before the expected move")
            : decision.reason + "; prediction arrived too late";
    }
    api_->recordDecision(decision);
}

} // namespace simu5g
