 #include "MecAppMigrationManager.h"
  #include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
  #include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"
  #include "nodes/mec/MECOrchestrator/ApplicationDescriptor/ApplicationDescriptor.h"
  #include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
  #include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
  #include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"
  #include "inet/networklayer/common/L3AddressResolver.h"


namespace simu5g {

using namespace omnetpp;

MecAppMigrationManager::MecAppMigrationManager(
    MecAppRegistry* mecRegistry,
    MecAppLifecycleManager* lifecycleManager,
    std::vector<cModule*>* mecHosts,
    std::unordered_map<std::string, cModule*>* mecHostIndex,
    cSimpleModule* owner,
    DecisionLogger* decisionLogger):
    mecAppRegistry_(mecRegistry),
    mecAppLifecycleManager_(lifecycleManager),
    mecHosts_(mecHosts),
    mecHostIndex_(mecHostIndex),
    owner_(owner),
    decisionLogger_(decisionLogger),
    requestCounter_(1) {}  // Start at 1 (0 reserved for non-migration operations)

MecAppMigrationManager::~MecAppMigrationManager() 
{
    pendingMigrations_.clear();
    // Cancel and delete all pending timeout messages
    for (auto& pair : timeoutMessages_) 
    {
      if (pair.second && pair.second->isScheduled()) 
      {
        owner_->cancelAndDelete(pair.second);
      }
    }
    timeoutMessages_.clear();
    standByList_.clear();
    standByUeIndex_.clear();
}

void MecAppMigrationManager::initialize(double migrationTime, double migrationTimeout) 
{
    migrationTime_ = migrationTime;
    migrationTimeout_ = migrationTimeout;

    EV << "MecAppMigrationManager::initialize" << endl;
    EV << "  Migration Time: " << migrationTime_ << "s" << endl;
    EV << "  Migration Timeout: " << migrationTimeout_ << "s" << endl;
}


/*
* CHECK IF MIGRATION IS NEEDED IN EDGE CASES 
* This is one of the two central methods of this Class. 
* Purpose:
*   When RAVENS detects a UE for the first time (lastMEHId is empty), we face
*   timing uncertainty: the UE might not have requested an app yet, might already
*   have an app on the correct MEH, or might have an app on the wrong MEH.
*   This method determines the current state and takes appropriate action.
* 
* Scenarios:
*   1. UE not yet instantiated → Return "no migration needed", wait for UE request
*   2. UE already on target MEH → Return "no migration needed", avoid unnecessary work
*   3. UE on different MEH → Trigger migration to correct location
* 
* Why this is needed:
*   - RAVENS updates and UE app requests are asynchronous
*   - RAVENS may start tracking a UE BEFORE or AFTER the app is already running
* 
* Without this check:
*   - Would attempt migrations on non-existent apps (errors)
*   - Would try to migrate apps already on correct MEH (wasteful)
*   - Would let apps on incorrect MEH
*/
MigrationResult MecAppMigrationManager::checkIfMigrationIsNeeded(std::string ueAddress, std::string newMEHId, std::string oldMEHId) 
{
    EV << "MecAppMigrationManager::checkIfMigrationIsNeeded" << endl;
    EV << "  UE: " << ueAddress << endl;
    EV << "  NewMEH: " << newMEHId << " OldMEH: " << oldMEHId << endl;

    // Remove "acr:" prefix if present to get IP address
    std::string ueIp = ueAddress;
    if (ueAddress.find("acr:") == 0) {
        ueIp = ueAddress.substr(4);
    }

    // Try to find app in registry
    auto result = mecAppRegistry_->findAppByUeAddress(ueIp);

    if (!result.found) {
        // App not yet instantiated - this is expected for new UEs
        EV << "MecAppMigrationManager::checkIfMigrationIsNeeded - App not found, waiting for UE request" << endl;
        MigrationResult noApp(false, "No application for this user", -1, 0, newMEHId, oldMEHId);
        noApp.nothingToDo = true;
        return noApp;
    }

    // App exists - check if it's already on the target MEH
    std::string currentMEHId = result.appEntry->mecHost->getName();

    if (currentMEHId == newMEHId) {
        EV << "MecAppMigrationManager::checkIfMigrationIsNeeded - App already on target MEH" << endl;
        MigrationResult alreadyThere(false, "App already on target MEH", result.contextId, 0, newMEHId, oldMEHId);
        alreadyThere.nothingToDo = true;
        return alreadyThere;
    }

    // Migration is needed - trigger it
    EV << "MecAppMigrationManager::checkIfMigrationIsNeeded - Migration needed from " << currentMEHId << " to " << newMEHId << endl;

    return migrateApp(ueAddress, newMEHId, currentMEHId);
}

std::string MecAppMigrationManager::getAppCurrentMEH(std::string ueAddress)
{
    std::string ueIp = ueAddress;
    if (ueAddress.find("acr:") == 0) {
        ueIp = ueAddress.substr(4);
    }
    auto result = mecAppRegistry_->findAppByUeAddress(ueIp);
    if (!result.found) {
        return "";
    }
    return result.appEntry->mecHost->getName();
}

MigrationResult MecAppMigrationManager::migrateApp(std::string ueAddress, std::string newMEHId, std::string oldMEHId)
{
    EV << "MecAppMigrationManager::migrateApplication - Starting migration" << endl;
    EV << "  UE: " << ueAddress << endl;
    EV << "  From: " << oldMEHId << " To: " << newMEHId << endl;

    // Remove "acr:" prefix if present
    std::string ueIp = ueAddress;
    if (ueAddress.find("acr:") == 0) {
        ueIp = ueAddress.substr(4);
    }

    // Step 1: Find app in registry
    auto lookupResult = mecAppRegistry_->findAppByUeAddress(ueIp);
    if (!lookupResult.found) {
        EV << "MecAppMigrationManager::migrateApplication - App not found for UE: " << ueAddress << endl;
        MigrationResult noApp(false, "No application for this user", -1, 0, newMEHId, oldMEHId);
        noApp.nothingToDo = true;
        return noApp;
    }

    int contextId = lookupResult.contextId;
    const MecAppRegistry::AppEntry* appEntry = lookupResult.appEntry;

    // Skip if app is already on target MEH
    std::string currentMEHId = appEntry->mecHost->getName();
    if (currentMEHId == newMEHId) {
        EV << "MecAppMigrationManager::migrateApp - App already on target MEH: " << newMEHId << endl;
        MigrationResult alreadyThere(false, "App already on target MEH", contextId, 0, newMEHId, oldMEHId);
        alreadyThere.nothingToDo = true;
        return alreadyThere;
    }


    // PENDING MIGRATION - CHECK IF THE UE IS ALREADY MIGRATING
    // PERFORMANCE IMPROVEMENT: O(1) lookup using index instead of O(n) linear search
    // Original code commented out for reference:
    // for (const auto& pair : standByList_) {
    //     if (pair.second.ueAddress == ueIp) { ... }
    // }

    auto indexIt = standByUeIndex_.find(ueIp);
    if (indexIt != standByUeIndex_.end()) {
        unsigned int existingRequest = indexIt->second;
        EV << "MecAppMigrationManager::migrateApp - Migration already in progress for UE: " << ueAddress << endl;
        EV << "  Current migration request: " << existingRequest << endl;
        EV << "  Queueing new migration: " << oldMEHId << " → " << newMEHId << endl;

        // Add to pending queue
        PendingMigration pending;
        pending.ueAddress = ueAddress;
        pending.newMEHId = newMEHId;
        pending.oldMEHId = oldMEHId;
        pending.requestTime = simTime();

        pendingMigrations_[ueIp].push(pending);

        EV << "MecAppMigrationManager::migrateApp - Migration queued. Queue size: " << pendingMigrations_[ueIp].size() << endl;

        return MigrationResult(false, "Migration queued - another migration in progress", contextId, 0, newMEHId, oldMEHId);
    }

    // Step 2: Find target MEH
    cModule* targetMEH = findMecHostByName(newMEHId);
    if (!targetMEH) {
        EV << "MecAppMigrationManager::migrateApplication - Target MEH not found: " << newMEHId << endl;
        return MigrationResult(false, "Target MEH not found: " + newMEHId, contextId, 0, newMEHId, oldMEHId);
    }

    // Step 3: Get application descriptor
    const ApplicationDescriptor* appDesc =
        mecAppLifecycleManager_->getApplicationDescriptor(appEntry->appDId);

    if (!appDesc) {
        EV << "MecAppMigrationManager::migrateApplication - App descriptor not found: " << appEntry->appDId << endl;
      return MigrationResult(false, "App descriptor not found", contextId, 0, newMEHId, oldMEHId);
    }

    // Step 4: Validate target MEH has sufficient resources
    if (!validateTargetMEH(targetMEH, *appDesc)) {
        EV << "MecAppMigrationManager::migrateApplication - Insufficient resources on: " << newMEHId << endl;
        return MigrationResult(false, "Insufficient resources on: " + newMEHId, contextId, 0, newMEHId, oldMEHId);
    }

    // Step 5: Perform migration
    return performMigration(contextId, targetMEH, ueIp, *appDesc);  
}

MigrationResult MecAppMigrationManager::completeMigration(UALCMPMessage* ackMsg)
{
    UpdateMEHAckMessage* updateMehAck = check_and_cast<UpdateMEHAckMessage*>(ackMsg);
    unsigned int requestNumber = updateMehAck->getRequestNumber();

    EV << "MecAppMigrationManager::completeMigration - ACK received for request: " << requestNumber << endl;

    // Find the standby entry
    auto it = standByList_.find(requestNumber);
    if (it == standByList_.end()) {
        std::cout << "ERROR completeMigration: Request " << requestNumber << " NOT FOUND in standByList!" << std::endl;
        EV << "MecAppMigrationManager::completeMigration - Request not found in standByList: " << requestNumber << endl;
        return MigrationResult(false, "Request not found in standByList", -1, requestNumber, "", "");
    }

    StandByElement standBy = it->second;
    simtime_t migrationDuration = simTime() - standBy.migrationStartTime;

    EV << "MecAppMigrationManager::completeMigration - Migration completed" << endl;
    EV << "  RequestNumber: " << requestNumber << endl;
    EV << "  ContextId: " << standBy.contextId << endl;
    EV << "  Duration: " << migrationDuration << "s" << endl;
    std::cout << "MIGRATION COMPLETED WITH CONTEXTID " << standBy.contextId << endl;

    // Cancel timeout
    cancelTimeout(requestNumber);

    // Destroy the instance the UE has just switched away from. Goes straight to the old
    // host rather than through the lifecycle manager — see terminateOldInstance().
    bool oldInstanceTerminated = terminateOldInstance(standBy);
    if (!oldInstanceTerminated) {
        EV << "MecAppMigrationManager::completeMigration - Old instance termination failed for request "
           << requestNumber << endl;
    }

    // Confirmed: the UE is on the new endpoint, the entry is Placed again —
    // on the new host, where recordMigration already points it.
    mecAppRegistry_->setAppState(standBy.contextId, MecAppRegistry::AppState::Placed);

    recordMigrationOutcome(standBy, DecisionOutcome::Success, oldInstanceTerminated, "");

    // Remove from standByList and index
    standByUeIndex_.erase(standBy.ueAddress);  // PERFORMANCE IMPROVEMENT: Maintain index
    standByList_.erase(it);

    EV << "MecAppMigrationManager::completeMigration - Migration completed successfully" << endl;

    // PENDING MIGRATION - CHECK IF THERE ARE PENDING MIGRATIONS FOR THIS UE
    auto pendingIt = pendingMigrations_.find(standBy.ueAddress);
    if (pendingIt != pendingMigrations_.end() && !pendingIt->second.empty()) {
        PendingMigration nextMigration = pendingIt->second.front();
        pendingIt->second.pop();

        EV << "MecAppMigrationManager::completeMigration - Processing queued migration" << endl;
        EV << "  UE: " << nextMigration.ueAddress << endl;
        EV << "  From: " << nextMigration.oldMEHId << " To: " << nextMigration.newMEHId << endl;
        EV << "  Queued at: " << nextMigration.requestTime << endl;
        EV << "  Remaining in queue: " << pendingIt->second.size() << endl;

        // Clean up queue if empty
        if (pendingIt->second.empty()) {
            pendingMigrations_.erase(pendingIt);
        }

        // Start the next migration (recursive call!!)
        // Note: We call migrateApp which will return a new MigrationResult
        // but we ignore it here since we're already returning success for the completed migration
        migrateApp(nextMigration.ueAddress, nextMigration.newMEHId, nextMigration.oldMEHId);
    }

    return MigrationResult(true, "Migration completed", standBy.contextId, requestNumber, "", "");
}

void MecAppMigrationManager::recordMigrationOutcome(const StandByElement& standBy,
                                                    DecisionOutcome outcome,
                                                    bool oldInstanceTerminated,
                                                    const std::string& reason)
{
    if (!decisionLogger_)
        return;

    OrchestrationDecision decision;
    decision.decidedAt = simTime();
    decision.ueAddress = standBy.ueAddress;
    decision.kind = DecisionKind::Migrate;
    decision.outcome = outcome;
    decision.requestNumber = standBy.request;

    // Where the application ended up, read from the entry rather than assumed.
    auto lookup = mecAppRegistry_->findAppByContextId(standBy.contextId);
    if (lookup.found && lookup.appEntry->mecHost)
        decision.toMEHId = lookup.appEntry->mecHost->getName();

    decision.reason = reason;
    if (!oldInstanceTerminated) {
        decision.reason += decision.reason.empty() ? "" : "; ";
        decision.reason += "old instance was not terminated";
    }

    decisionLogger_->record(decision);
}

bool MecAppMigrationManager::terminateOldInstance(const StandByElement& standBy)
{
    if (standBy.oldMecpm == nullptr) {
        EV << "MecAppMigrationManager::terminateOldInstance - no source platform manager "
           << "recorded for request " << standBy.request << ", cannot terminate" << endl;
        return false;
    }

    MecPlatformManager* oldMecpm = check_and_cast<MecPlatformManager*>(standBy.oldMecpm);

    DeleteAppMessage* deleteAppMsg = new DeleteAppMessage();
    deleteAppMsg->setUeAppID(standBy.mecUeAppID);

    // MecPlatformManager::terminateMEApp() deletes the message itself — do not delete it here.
    bool terminated = oldMecpm->terminateMEApp(deleteAppMsg);

    if (terminated)
        EV << "MecAppMigrationManager::terminateOldInstance - old instance for UE app "
           << standBy.mecUeAppID << " terminated on " << standBy.oldMecpm->getFullPath() << endl;
    else
        EV << "MecAppMigrationManager::terminateOldInstance - WARNING: no instance found for "
           << "UE app " << standBy.mecUeAppID << " on " << standBy.oldMecpm->getFullPath() << endl;

    return terminated;
}

void MecAppMigrationManager::handleMigrationTimeout(unsigned int requestNumber)
{
    EV << "MecAppMigrationManager::handleMigrationTimeout - Timeout for request: " << requestNumber << endl;

    // Check if still in standByList
    auto it = standByList_.find(requestNumber);
    if (it == standByList_.end()) {
        EV << "MecAppMigrationManager::handleMigrationTimeout - Request already completed: " << requestNumber << endl;
        return;
    }

    StandByElement standBy = it->second;
    simtime_t waitTime = simTime() - standBy.migrationStartTime;

    EV << "MecAppMigrationManager::handleMigrationTimeout - Force-completing migration" << endl;
    EV << "  RequestNumber: " << requestNumber << endl;
    EV << "  ContextId: " << standBy.contextId << endl;
    EV << "  Time waiting: " << waitTime << "s" << endl;

    // Force complete the migration
    forceCompleteMigration(requestNumber, "Timeout - UE did not respond");
}

cModule* MecAppMigrationManager::findMecHostByName(const std::string& mehName)
{
    // PERFORMANCE IMPROVEMENT: O(1) lookup using index instead of O(n) linear search
    // Original code commented out for reference:
    // for (auto mecHost : *mecHosts_) {
    //     if (std::string(mecHost->getName()) == mehName) {
    //         return mecHost;
    //     }
    // }

    auto it = mecHostIndex_->find(mehName);
    if (it != mecHostIndex_->end()) {
        return it->second;
    }
    return nullptr;
}

bool MecAppMigrationManager::validateTargetMEH(cModule* targetMEH, const ApplicationDescriptor& appDesc)
{
    if (!targetMEH) {
        return false;
    }

    // Get VIM submodule
    VirtualisationInfrastructureManager* vim = check_and_cast<VirtualisationInfrastructureManager*>(targetMEH->getSubmodule("vim"));

    if (!vim) {
        EV << "MecAppMigrationManager::validateTargetMEH - VIM not found on target MEH" << endl;
        return false;
    }

    // Check resource availability
    ResourceDescriptor resources = appDesc.getVirtualResources();
    bool isAllocable = vim->isAllocable(resources.ram, resources.disk, resources.cpu);

    if (!isAllocable) {
        EV << "MecAppMigrationManager::validateTargetMEH - Insufficient resources" << endl;
        EV << "  Required - RAM: " << resources.ram << " Disk: " << resources.disk << " CPU: " << resources.cpu << endl;
    }

    return isAllocable;
}

MigrationResult MecAppMigrationManager::performMigration(int contextId, cModule* targetMEH, const std::string& ueAddress, const ApplicationDescriptor& appDesc)
{
    EV << "MecAppMigrationManager::performMigration - Performing migration for contextId: " << contextId << endl;

    // Get current app entry
    auto lookupResult = mecAppRegistry_->findAppByContextId(contextId);
    if (!lookupResult.found) {
        return MigrationResult(false, "App entry not found", contextId, 0, "", "");
    }

    const MecAppRegistry::AppEntry* oldAppEntry = lookupResult.appEntry;
    cModule* oldMecpm = oldAppEntry->mecpm;
    std::string oldMEHId = oldAppEntry->mecHost->getName();
    std::string newMEHId = targetMEH->getName();

    // Increment request counter
    unsigned int requestNumber = requestCounter_++;

    // In flight from here: new instance being brought up, UE not yet
    // retargeted. On any failure below this reverts to Placed — the old
    // instance never stopped serving.
    mecAppRegistry_->setAppState(contextId, MecAppRegistry::AppState::Migrating);

    EV << "MecAppMigrationManager::performMigration - Migration details:" << endl;
    EV << "  Old MEH: " << oldMEHId << endl;
    EV << "  New MEH: " << newMEHId << endl;
    EV << "  Request Number: " << requestNumber << endl;

    // Step 1: Instantiate app on new MEH using MEC Platform Manager
    CreateAppMessage* createAppMsg = new CreateAppMessage();
    createAppMsg->setUeAppID(oldAppEntry->mecUeAppID);
    createAppMsg->setMEModuleName(appDesc.getAppName().c_str());
    createAppMsg->setMEModuleType(appDesc.getAppProvider().c_str());
    createAppMsg->setRequiredCpu(appDesc.getVirtualResources().cpu);
    createAppMsg->setRequiredRam(appDesc.getVirtualResources().ram);
    createAppMsg->setRequiredDisk(appDesc.getVirtualResources().disk);

    if (!appDesc.getOmnetppServiceRequired().empty()) {
        createAppMsg->setRequiredService(appDesc.getOmnetppServiceRequired().c_str());
    } else {
        createAppMsg->setRequiredService("NULL");
    }

    // Create new context ID for the new instance
    int newContextId = mecAppLifecycleManager_->getContextIdCounter();
    createAppMsg->setContextId(newContextId);

    // Get MEC Platform Manager from target MEH
    cModule* newMecpm = targetMEH->getSubmodule("mecPlatformManager");
    if (!newMecpm) {
        mecAppRegistry_->setAppState(contextId, MecAppRegistry::AppState::Placed);
        return MigrationResult(false, "MEC Platform Manager not found on target MEH", contextId, 0, newMEHId, oldMEHId);
    }

    MecPlatformManager* mecpm = check_and_cast<MecPlatformManager*>(newMecpm);

    // Instantiate the app
    MecAppInstanceInfo* appInfo = mecpm->instantiateMEApp(createAppMsg);

    if (!appInfo || !appInfo->status) {
        mecAppRegistry_->setAppState(contextId, MecAppRegistry::AppState::Placed);
        return MigrationResult(false, "Instantiation on new MEH failed", contextId, 0, newMEHId, oldMEHId);
    }

    EV << "MecAppMigrationManager::performMigration - App instantiated on new MEH" << endl;
    EV << "  New endpoint: " << appInfo->endPoint.addr.str() << ":" << appInfo->endPoint.port << endl;

    // Step 2: record the move on the same registry entry. Same entry, same
    // contextId — migration changes where the app runs, not which app it is.
    // newContextId was only needed for a unique platform-side module name and
    // is not the registry's identity.
    if (!mecAppRegistry_->recordMigration(contextId, targetMEH, appInfo)) {
        EV << "MecAppMigrationManager::performMigration - Failed to update registry" << endl;
        // Try to cleanup the new instance
        DeleteAppMessage* deleteMsg = new DeleteAppMessage();
        deleteMsg->setUeAppID(oldAppEntry->mecUeAppID);
        mecpm->terminateMEApp(deleteMsg);
        mecAppRegistry_->setAppState(contextId, MecAppRegistry::AppState::Placed);
        return MigrationResult(false, "Failed to update registry", contextId, 0, newMEHId, oldMEHId);
    }

    // Increment context ID counter
    mecAppLifecycleManager_->incrementContextIdCounter();

    EV << "MecAppMigrationManager::performMigration - Registry updated" << endl;

    // Step 3: Send MEH change request to UE
    std::string newMehAddress = appInfo->endPoint.addr.str();
    int newPort = appInfo->endPoint.port;

    sendMehChangeRequest(ueAddress, newMehAddress, newPort, requestNumber);

    //std::cout << "DEBUG performMigration: Adding to standByList with ueAddress: '" << ueAddress << "'" << std::endl;
    //std::cout << "DEBUG performMigration: requestNumber: " << requestNumber << std::endl;

    // Step 4: Add to standByList
    StandByElement standBy;
    standBy.request = requestNumber;
    standBy.oldMecpm = oldMecpm;
    standBy.mecUeAppID = oldAppEntry->mecUeAppID;
    standBy.migrationStartTime = simTime();
    standBy.contextId = contextId;  // entry identity — the same across the whole migration
    standBy.ueAddress = ueAddress;

    standByList_[requestNumber] = standBy;
    standByUeIndex_[ueAddress] = requestNumber;  // PERFORMANCE IMPROVEMENT: Maintain index

    // Step 5: Schedule timeout
    scheduleTimeout(requestNumber);

    // New instance up, endpoint recorded, retarget on its way to the UE.
    mecAppRegistry_->setAppState(contextId, MecAppRegistry::AppState::AwaitingConfirmation);

    EV << "MecAppMigrationManager::performMigration - Migration initiated successfully" << endl;

    return MigrationResult(true, "Migration initiated", contextId, requestNumber, newMEHId, oldMEHId);
}

void MecAppMigrationManager::sendMehChangeRequest(std::string ueIp, std::string newMehAddress, int newPort, unsigned int requestNumber)
{
    EV << "MecAppMigrationManager::sendMehChangeRequest" << endl;
    EV << "  UE: " << ueIp << endl;
    EV << "  New endpoint: " << newMehAddress << ":" << newPort << endl;
    EV << "  Request: " << requestNumber << endl;

    UpdateMEHMessage* mehChangeRequest = new UpdateMEHMessage();
    mehChangeRequest->setName("UpdateMEHMessage");
    mehChangeRequest->setType(UPDATE_MEH_IP);
    mehChangeRequest->setUeIpAddress(ueIp.c_str());
    mehChangeRequest->setNewMehIpAddress(newMehAddress.c_str());
    mehChangeRequest->setNewMehPort(newPort);
    mehChangeRequest->setRequestNumber(requestNumber);

    // Send delayed by migrationTime
    owner_->sendDelayed(mehChangeRequest, migrationTime_, "toUALCMP");
}

void MecAppMigrationManager::scheduleTimeout(unsigned int requestNumber)
{
    EV << "MecAppMigrationManager::scheduleTimeout - Scheduling timeout for request: " << requestNumber << endl;

    MigrateTimeoutMessage* timeoutMsg = new MigrateTimeoutMessage();
    timeoutMsg->setName("MigrationTimeout");
    timeoutMsg->setRequestNumber(requestNumber);

    // Schedule timeout
    owner_->scheduleAt(simTime() + + migrationTime_ + migrationTimeout_, timeoutMsg);

    // Store reference for cancellation
    timeoutMessages_[requestNumber] = timeoutMsg;
}

void MecAppMigrationManager::cancelTimeout(unsigned int requestNumber)
{
    auto it = timeoutMessages_.find(requestNumber);
    if (it != timeoutMessages_.end()) {
        EV << "MecAppMigrationManager::cancelTimeout - Cancelling timeout for request: " << requestNumber << endl;

        if (it->second && it->second->isScheduled()) {
            owner_->cancelAndDelete(it->second);
        }
        timeoutMessages_.erase(it);
    }
}

void MecAppMigrationManager::forceCompleteMigration(unsigned int requestNumber, const std::string& reason)
{
    EV << "MecAppMigrationManager::forceCompleteMigration - " << reason << endl;
    EV << "  Request: " << requestNumber << endl;

    auto it = standByList_.find(requestNumber);
    if (it == standByList_.end()) {
        return;
    }

    StandByElement standBy = it->second;

    // Terminate old instance (same reasoning as completeMigration)
    bool oldInstanceTerminated = terminateOldInstance(standBy);
    if (!oldInstanceTerminated) {
        EV << "MecAppMigrationManager::forceCompleteMigration - Termination failed for request "
           << requestNumber << endl;
    }

    // Clean up timeout message if not already fired
    cancelTimeout(requestNumber);

    // Current policy force-completes on timeout — old instance terminated,
    // entry Placed on the new host. Whether a timeout should instead resolve
    // to the old host is item 3 of the plan, not this round.
    mecAppRegistry_->setAppState(standBy.contextId, MecAppRegistry::AppState::Placed);

    // Failed rather than Success: the UE never confirmed, so this migration
    // completed without knowing the user followed it.
    recordMigrationOutcome(standBy, DecisionOutcome::Failed, oldInstanceTerminated, reason);

    // Remove from standByList and index
    standByUeIndex_.erase(standBy.ueAddress);  // PERFORMANCE IMPROVEMENT: Maintain index
    standByList_.erase(it);

    // PENDING MIGRATION - CHECK IF THERE ARE PENDING MIGRATIONS FOR THIS UE
    auto pendingIt = pendingMigrations_.find(standBy.ueAddress);
    if (pendingIt != pendingMigrations_.end() && !pendingIt->second.empty()) {
        PendingMigration nextMigration = pendingIt->second.front();
        pendingIt->second.pop();

        EV << "MecAppMigrationManager::forceCompleteMigration - Processing queued migration" << endl;
        EV << "  UE: " << nextMigration.ueAddress << endl;
        EV << "  From: " << nextMigration.oldMEHId << " To: " << nextMigration.newMEHId << endl;
        EV << "  Queued at: " << nextMigration.requestTime << endl;
        EV << "  Remaining in queue: " << pendingIt->second.size() << endl;

        // Clean up queue if empty
        if (pendingIt->second.empty()) {
            pendingMigrations_.erase(pendingIt);
        }

        // Start the next migration
        migrateApp(nextMigration.ueAddress, nextMigration.newMEHId, nextMigration.oldMEHId);
    }

    EV << "MecAppMigrationManager::forceCompleteMigration - Migration force-completed" << endl;
}

} // namespace simu5g
