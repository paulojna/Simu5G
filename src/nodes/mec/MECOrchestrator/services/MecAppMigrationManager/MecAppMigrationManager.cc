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
    cSimpleModule* owner):
    mecAppRegistry_(mecRegistry),
    mecAppLifecycleManager_(lifecycleManager),
    mecHosts_(mecHosts),
    mecHostIndex_(mecHostIndex),
    owner_(owner),
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
        return MigrationResult(false, "No migration needed", -1, 0, newMEHId, oldMEHId);
    }

    // App exists - check if it's already on the target MEH
    std::string currentMEHId = result.appEntry->mecHost->getName();

    if (currentMEHId == newMEHId) {
        EV << "MecAppMigrationManager::checkIfMigrationIsNeeded - App already on target MEH" << endl;
        return MigrationResult(false, "No migration needed", result.contextId, 0, newMEHId, oldMEHId);
    }

    // Migration is needed - trigger it
    EV << "MecAppMigrationManager::checkIfMigrationIsNeeded - Migration needed from " << currentMEHId << " to " << newMEHId << endl;

    return migrateApp(ueAddress, newMEHId, currentMEHId);
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
        return MigrationResult(false, "App not found for UE: " + ueAddress, -1, 0, newMEHId, oldMEHId);
    }

    int contextId = lookupResult.contextId;
    const MecAppRegistry::AppEntry* appEntry = lookupResult.appEntry;

    // PENDING MIGRATION - NEW CHECK! Skip if app is already on target MEH
    std::string currentMEHId = appEntry->mecHost->getName();
    if (currentMEHId == newMEHId) {
        EV << "MecAppMigrationManager::migrateApp - App already on target MEH: " << newMEHId << endl;
        EV << "  Skipping redundant migration" << endl;
        return MigrationResult(false, "App already on target MEH", contextId, 0, newMEHId, oldMEHId);
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

    // Terminate old app instance via LifecycleManager
    LifecycleResult terminationResult = mecAppLifecycleManager_->stopApplication(standBy.contextId);

    if (!terminationResult.success) {
        EV << "MecAppMigrationManager::completeMigration - Old instance termination failed: " << terminationResult.errorMessage << endl;
    }

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
        return MigrationResult(false, "MEC Platform Manager not found on target MEH", contextId, 0, newMEHId, oldMEHId);
    }

    MecPlatformManager* mecpm = check_and_cast<MecPlatformManager*>(newMecpm);

    // Instantiate the app
    MecAppInstanceInfo* appInfo = mecpm->instantiateMEApp(createAppMsg);

    if (!appInfo || !appInfo->status) {
        return MigrationResult(false, "Instantiation on new MEH failed", contextId, 0, newMEHId, oldMEHId);
    }

    EV << "MecAppMigrationManager::performMigration - App instantiated on new MEH" << endl;
    EV << "  New endpoint: " << appInfo->endPoint.addr.str() << ":" << appInfo->endPoint.port << endl;

    // Step 2: Update registry with new app instance
    MecAppRegistry::AppEntry newAppEntry;
    newAppEntry.contextId = newContextId;
    newAppEntry.appDId = appDesc.getAppDId();
    newAppEntry.mecAppName = appDesc.getAppName();
    newAppEntry.mecUeAppID = oldAppEntry->mecUeAppID;
    newAppEntry.mecHost = targetMEH;
    newAppEntry.vim = targetMEH->getSubmodule("vim");
    newAppEntry.mecpm = newMecpm;
    newAppEntry.ueSymbolicAddress = oldAppEntry->ueSymbolicAddress;
    newAppEntry.ueAddress = oldAppEntry->ueAddress;
    newAppEntry.uePort = oldAppEntry->uePort;
    newAppEntry.mecAppAddress = appInfo->endPoint.addr;
    newAppEntry.mecAppPort = appInfo->endPoint.port;
    newAppEntry.mecAppInstanceId = appInfo->instanceId;
    newAppEntry.isEmulated = false;
    newAppEntry.lastAckStartSeqNum = oldAppEntry->lastAckStartSeqNum;
    newAppEntry.lastAckStopSeqNum = oldAppEntry->lastAckStopSeqNum;

    // Unregister old app entry
    mecAppRegistry_->unregisterApp(contextId);

    // Register new app entry
    bool registered = mecAppRegistry_->registerApp(newAppEntry);
    if (!registered) {
        EV << "MecAppMigrationManager::performMigration - Failed to register new app entry" << endl;
        // Try to cleanup the new instance
        DeleteAppMessage* deleteMsg = new DeleteAppMessage();
        deleteMsg->setUeAppID(oldAppEntry->mecUeAppID);
        mecpm->terminateMEApp(deleteMsg);
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
    standBy.contextId = contextId;  // Old context ID for termination
    standBy.ueAddress = ueAddress;

    standByList_[requestNumber] = standBy;
    standByUeIndex_[ueAddress] = requestNumber;  // PERFORMANCE IMPROVEMENT: Maintain index

    // Step 5: Schedule timeout
    scheduleTimeout(requestNumber);

    EV << "MecAppMigrationManager::performMigration - Migration initiated successfully" << endl;

    return MigrationResult(true, "Migration initiated", newContextId, requestNumber, newMEHId, oldMEHId);
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

    // Terminate old instance
    LifecycleResult terminationResult = mecAppLifecycleManager_->stopApplication(standBy.contextId);

    if (!terminationResult.success) {
        EV << "MecAppMigrationManager::forceCompleteMigration - Termination failed: " << terminationResult.errorMessage << endl;
    }

    // Clean up timeout message if not already fired
    cancelTimeout(requestNumber);

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
