#include "MigrateOnPrediction.h"
#include "nodes/mec/MECOrchestrator/services/MecAppMigrationManager/MecAppMigrationManager.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"

/*
 * MigrateOnPrediction Strategy
 *
 * Proactive migration based on ML predictions from Flask.
 * See header for full documentation.
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

    // Close migration log
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

// ─── Reactive path: handle entry/exit from USERS_UPDATE ───

void MigrateOnPrediction::reactOnUpdate(const UserMEHUpdate& update)
{
    std::cout << "[MigrateOnPrediction t=" << simTime() << "] "
              << "UE=" << update.getAddress()
              << " lastMEH='" << update.getLastMEHId() << "'"
              << " newMEH='" << update.getNewMEHId() << "'" << std::endl;

    // Scenario 1: Exit — UE left the system
    if (update.getNewMEHId() == "")
    {
        EV << "MigrateOnPrediction::reactOnUpdate - UE " << update.getAddress() << " exited system" << endl;

        // Cancel any scheduled prediction for this user (only exits cancel!)
        auto it = scheduledPredictions_.find(update.getAddress());
        if (it != scheduledPredictions_.end())
        {
            EV << "MigrateOnPrediction::reactOnUpdate - Cancelling scheduled prediction for exiting UE "
               << update.getAddress() << endl;
            std::cout << "[MigrateOnPrediction t=" << simTime()
                      << "] Cancelled prediction for exiting UE " << update.getAddress() << std::endl;
            owner_->cancelAndDelete(it->second);
            scheduledPredictions_.erase(it);
        }

        // Remove the app from the system
        api_->removeAppFromSystem(update.getAddress(), update.getLastMEHId());
    }
    // Scenario 2: Entry — new UE detected by RAVENS
    else if (update.getLastMEHId() == "" && update.getNewMEHId() != "")
    {
        EV << "MigrateOnPrediction::reactOnUpdate - New UE " << update.getAddress()
           << " detected at " << update.getNewMEHId() << endl;

        MigrationResult result = api_->checkIfMigrationIsNeeded(
            update.getAddress(), update.getNewMEHId(), update.getLastMEHId());

        if (result.success) {
            EV << "MigrateOnPrediction::reactOnUpdate - Migration initiated for new UE, request #"
               << result.requestNumber << endl;
        } else {
            EV << "MigrateOnPrediction::reactOnUpdate - " << result.errorMessage << endl;
        }
    }
    // Scenario 3: Handover — reactive fallback for unpredicted handovers
    // Flask needs ~30s of observations before it can predict. During this
    // warmup period (or when the model fails to predict), handovers go
    // undetected. This reactive fallback catches them.
    // checkIfMigrationIsNeeded is idempotent: if Flask already migrated
    // proactively, the app is already on the correct MEH → "no migration needed".
    else if (update.getNewMEHId() != "" && update.getLastMEHId() != ""
             && update.getNewMEHId() != update.getLastMEHId())
    {
        EV << "MigrateOnPrediction::reactOnUpdate - Reactive fallback: handover detected" << endl;
        EV << "  UE: " << update.getAddress() << endl;
        EV << "  From: " << update.getLastMEHId() << " To: " << update.getNewMEHId() << endl;

        MigrationResult result = api_->checkIfMigrationIsNeeded(
            update.getAddress(), update.getNewMEHId(), update.getLastMEHId());

        if (result.success) {
            EV << "MigrateOnPrediction::reactOnUpdate - Reactive migration initiated, request #"
               << result.requestNumber << endl;
            std::cout << "[MigrateOnPrediction t=" << simTime()
                      << "] REACTIVE fallback migration for UE " << update.getAddress()
                      << " from " << update.getLastMEHId() << " to " << update.getNewMEHId() << std::endl;
        } else {
            // Expected when Flask already handled this proactively
            EV << "MigrateOnPrediction::reactOnUpdate - " << result.errorMessage << endl;
        }

        // Log reactive handover
        if (logFile_.is_open()) {
            logFile_ << simTime() << ","
                     << update.getAddress() << ","
                     << "REACTIVE_HANDOVER" << ","
                     << update.getLastMEHId() << ","
                     << update.getNewMEHId() << ","
                     << (result.success ? "INITIATED" : "NO_MIGRATION_NEEDED") << std::endl;
        }
    }
    else
    {
        EV << "MigrateOnPrediction::reactOnUpdate - No action for UE " << update.getAddress() << endl;
    }
}

// ─── Proactive path: schedule migrations from Flask predictions ───

void MigrateOnPrediction::reactOnUpdate(const std::vector<MigrationPrediction>& predictions)
{
    EV << "MigrateOnPrediction::reactOnUpdate - Processing " << predictions.size() << " predictions" << endl;
    std::cout << "[MigrateOnPrediction t=" << simTime() << "] Processing "
              << predictions.size() << " predictions" << std::endl;

    for (const auto& pred : predictions)
    {
        const std::string& ueAddress = pred.getUeAddress();

        // Cancel any previously scheduled prediction for this user
        // (newer prediction replaces older one)
        auto it = scheduledPredictions_.find(ueAddress);
        if (it != scheduledPredictions_.end())
        {
            EV << "MigrateOnPrediction - Replacing existing prediction for UE " << ueAddress << endl;
            owner_->cancelAndDelete(it->second);
            scheduledPredictions_.erase(it);
        }

        // Compute adjusted delay so migration COMPLETES at the predicted time
        //
        // targetTime = when the UE will arrive at new AP (prediction timestamp + migration delay)
        // We want migration to complete by targetTime, so start it migrationTime_ earlier
        //
        // adjustedDelay = targetTime - simTime() - migrationTime_
        //
        simtime_t targetTime = pred.getTimestamp() + pred.getMigrationDelay();
        simtime_t adjustedDelay = targetTime - simTime() - migrationTime_;

        // Clamp to 0 if prediction arrived late (execute immediately)
        if (adjustedDelay < 0)
            adjustedDelay = 0;

        // Create self-message using existing MigrateAppMessage
        // type: 0 = migration, 1 = exit
        MigrateAppMessage* msg = new MigrateAppMessage("ScheduledMigration");
        msg->setUeAddress(ueAddress.c_str());
        msg->setNewMEHId(pred.getTargetMEHId().c_str());
        msg->setOldMEHId(pred.getCurrentMEHId().c_str());
        msg->setType(pred.isExitPrediction() ? 1 : 0);

        // Schedule and track
        owner_->scheduleAt(simTime() + adjustedDelay, msg);
        scheduledPredictions_[ueAddress] = msg;

        std::cout << "[MigrateOnPrediction t=" << simTime() << "] Scheduled "
                  << (pred.isExitPrediction() ? "EXIT" : "MIGRATION")
                  << " for UE " << ueAddress
                  << " | target=" << pred.getTargetMEHId()
                  << " | delay=" << adjustedDelay << "s"
                  << " | fires at t=" << (simTime() + adjustedDelay) << std::endl;
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

    if (type == 1)
    {
        // Predicted exit
        std::cout << "[MigrateOnPrediction t=" << simTime()
                  << "] Executing predicted EXIT for UE " << ueAddress << std::endl;
        EV << "MigrateOnPrediction::handleScheduledEvent - Executing predicted EXIT for UE "
           << ueAddress << endl;

        api_->removeAppFromSystem(ueAddress, oldMEHId);
    }
    else
    {
        // Predicted migration
        std::cout << "[MigrateOnPrediction t=" << simTime()
                  << "] Executing predicted MIGRATION for UE " << ueAddress
                  << " from " << oldMEHId << " to " << newMEHId << std::endl;
        EV << "MigrateOnPrediction::handleScheduledEvent - Executing predicted MIGRATION for UE "
           << ueAddress << " from " << oldMEHId << " to " << newMEHId << endl;

        MigrationResult result = api_->migrateApp(ueAddress, newMEHId, oldMEHId);

        if (!result.success) {
            EV << "MigrateOnPrediction::handleScheduledEvent - Migration failed: "
               << result.errorMessage << endl;
            std::cout << "[MigrateOnPrediction t=" << simTime()
                      << "] Migration failed for UE " << ueAddress
                      << ": " << result.errorMessage << std::endl;
        } else {
            EV << "MigrateOnPrediction::handleScheduledEvent - Migration initiated, request #"
               << result.requestNumber << endl;
            std::cout << "[MigrateOnPrediction t=" << simTime()
                      << "] Migration initiated for UE " << ueAddress
                      << ", request #" << result.requestNumber << std::endl;
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
