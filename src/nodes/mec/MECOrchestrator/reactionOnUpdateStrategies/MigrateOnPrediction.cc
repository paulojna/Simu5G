#include "MigrateOnPrediction.h"
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
    // Open CSV log file with run number for post-simulation analysis
    int runNumber = cSimulation::getActiveSimulation()->getActiveEnvir()->getConfigEx()->getActiveRunNumber();
    std::string filename = "migration_log_run" + std::to_string(runNumber) + ".csv";
    logFile_.open(filename, std::ios::out | std::ios::trunc);
    if (logFile_.is_open()) {
        logFile_ << "sim_time,ue_address,type,from_meh,to_meh,result" << std::endl;
        std::cout << "[MigrateOnPrediction] Opened migration log: " << filename << std::endl;
    } else {
        std::cerr << "[MigrateOnPrediction] WARNING: Could not open migration log: " << filename << std::endl;
    }
}

MigrateOnPrediction::~MigrateOnPrediction()
{
    // Cancel all outstanding scheduled predictions to prevent simulation cleanup errors
    for (auto& [address, msg] : scheduledPredictions_) {
        owner_->cancelAndDelete(msg);
    }
    scheduledPredictions_.clear();

    std::cout << "[MigrateOnPrediction] predictions that arrived too late to schedule properly: "
              << latePredictions_ << std::endl;

    // Close migration log
    if (logFile_.is_open()) {
        logFile_.close();
    }
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
            owner_->cancelAndDelete(it->second);
            scheduledPredictions_.erase(it);
        }

        api_->removeAppFromSystem(event.ueAddress, event.fromMEHId);
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

        // Log reactive handover
        if (logFile_.is_open()) {
            logFile_ << simTime() << ","
                     << event.ueAddress << ","
                     << "REACTIVE_HANDOVER" << ","
                     << event.fromMEHId << ","
                     << event.toMEHId << ","
                     << (result.success ? "INITIATED" : "NO_MIGRATION_NEEDED") << std::endl;
        }
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
            MigrateAppMessage* existingMsg = check_and_cast<MigrateAppMessage*>(it->second);
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
            owner_->cancelAndDelete(it->second);
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
        if (adjustedDelay < 0) {
            latePredictions_++;
            EV_WARN << "MigrateOnPrediction - prediction for " << ueAddress << " arrived "
                    << -adjustedDelay << "s too late to complete on time, starting now" << endl;
            if (logFile_.is_open()) {
                logFile_ << simTime() << "," << ueAddress << "," << "PROACTIVE_MIGRATION" << ","
                         << pred.fromMEHId << "," << pred.toMEHId << "," << "LATE" << std::endl;
            }
            adjustedDelay = 0;
        }

        // Create self-message using existing MigrateAppMessage
        MigrateAppMessage* msg = new MigrateAppMessage("ScheduledMigration");
        msg->setUeAddress(ueAddress.c_str());
        msg->setNewMEHId(pred.toMEHId.c_str());
        msg->setOldMEHId(pred.fromMEHId.c_str());
        msg->setType(pred.isExitPrediction() ? MIGRATE_APP_EXIT : MIGRATE_APP_MOVE);

        // Schedule and track
        owner_->scheduleAt(simTime() + adjustedDelay, msg);
        scheduledPredictions_[ueAddress] = msg;

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

    // Remove from tracking map
    // (message itself will be deleted by MecOrchestrator::handleMessage after this returns)
    scheduledPredictions_.erase(ueAddress);

    if (type == MIGRATE_APP_EXIT)
    {
        // Predicted exit
        EV << "MigrateOnPrediction::handleScheduledEvent - Executing predicted EXIT for UE "
           << ueAddress << endl;

        api_->removeAppFromSystem(ueAddress, oldMEHId);
    }
    else
    {
        // Predicted migration — verify app is still at expected source MEH
        std::string currentMEH = api_->getAppCurrentMEH(ueAddress);
        if (!currentMEH.empty() && currentMEH != oldMEHId)
        {
            EV << "MigrateOnPrediction::handleScheduledEvent - Stale prediction for UE "
               << ueAddress << " (expected " << oldMEHId << ", app at " << currentMEH << ")" << endl;

            if (logFile_.is_open()) {
                logFile_ << simTime() << ","
                         << ueAddress << ","
                         << "PROACTIVE_MIGRATION" << ","
                         << oldMEHId << ","
                         << newMEHId << ","
                         << "STALE" << std::endl;
            }
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

        // Log proactive migration
        if (logFile_.is_open()) {
            logFile_ << simTime() << ","
                     << ueAddress << ","
                     << "PROACTIVE_MIGRATION" << ","
                     << oldMEHId << ","
                     << newMEHId << ","
                     << (result.success ? "INITIATED" : "FAILED") << std::endl;
        }
    }
}

} // namespace simu5g
