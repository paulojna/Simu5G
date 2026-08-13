#ifndef __MECAPPMIGRATIONMANAGER_H_
#define __MECAPPMIGRATIONMANAGER_H_

#include "nodes/mec/MECOrchestrator/services/MecAppRegistry/MecAppRegistry.h"
#include "nodes/mec/MECOrchestrator/services/MecAppLifecycleManager/MecAppLifecycleManager.h"
#include "nodes/mec/MECOrchestrator/services/DecisionLogger/DecisionLogger.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include <map>
#include <string>
#include <queue>
#include <vector>

namespace simu5g {
using namespace omnetpp;

class MecPlatformManager;
class VirtualisationInfrastructureManager;
class ApplicationDescriptor;

struct MigrationResult {
    bool success;
    std::string errorMessage;
    int contextId;
    unsigned int requestNumber;
    std::string newMEHId;
    std::string oldMEHId;

    // No action was called for — there is no application, or it is already on
    // the target host — as opposed to an action that was called for and failed.
    // The decision log needs the two apart: "nothing to do" is the common,
    // healthy answer, and counting it as a failure would bury the real ones.
    bool nothingToDo = false;

    MigrationResult()
    : success(false), contextId(-1), requestNumber(0) {}

    MigrationResult(bool success, const std::string& errorMessage, int contextId, unsigned int requestNumber, const std::string& newMEHId, const std::string& oldMEHId) 
    : success(success), errorMessage(errorMessage), contextId(contextId), requestNumber(requestNumber), newMEHId(newMEHId), oldMEHId(oldMEHId) {}
};

/*
    StandByElement represents a pending migration request for a MEC application. 
    It is used whilst the Orchestrator waits for the ACK from the UE.
*/
struct StandByElement
{
    unsigned int request;
    cModule* oldMecpm;
    int mecUeAppID; 
    simtime_t migrationStartTime;
    int contextId;
    std::string ueAddress;

    StandByElement()
    : request(0), oldMecpm(nullptr), mecUeAppID(-1), migrationStartTime(0), contextId(-1), ueAddress("") {}

};

class MecAppMigrationManager {
public:
    MecAppMigrationManager(
      MecAppRegistry* appRegistry,
      MecAppLifecycleManager* lifecycleManager,
      std::vector<cModule*>* mecHosts,
      std::unordered_map<std::string, cModule*>* mecHostIndex,  // PERFORMANCE IMPROVEMENT: O(1) lookup
      cSimpleModule* owner,
      DecisionLogger* decisionLogger   // may be null: no decision log configured
    );

    virtual ~MecAppMigrationManager();

    void initialize(double migrationTime, double migrationTimeout);

    MigrationResult checkIfMigrationIsNeeded(std::string ueAddress, std::string newMEHId, std::string oldMEHId);
    MigrationResult migrateApp(std::string ueAddress, std::string newMEHId, std::string oldMEHId);
    MigrationResult completeMigration(UALCMPMessage* ackMsg);
    std::string getAppCurrentMEH(std::string ueAddress);

   // Timeout handling
    void handleMigrationTimeout(unsigned int requestNumber);

    double getMigrationTime() const { return migrationTime_; }
    double getMigrationTimeout() const { return migrationTimeout_; }

private:
    struct PendingMigration {
        std::string ueAddress;
        std::string newMEHId;
        std::string oldMEHId;
        simtime_t requestTime;

        PendingMigration() : requestTime(0) {}
    };

    std::map<std::string, std::queue<PendingMigration>> pendingMigrations_;

    // Dependencies
    MecAppRegistry* mecAppRegistry_;
    MecAppLifecycleManager* mecAppLifecycleManager_;
    std::vector<cModule*>* mecHosts_;
    std::unordered_map<std::string, cModule*>* mecHostIndex_;  // PERFORMANCE IMPROVEMENT: O(1) lookup
    cSimpleModule* owner_;  // For sending self-messages
    DecisionLogger* decisionLogger_;  // not owned; null when no log is configured

    // Configuration
    double migrationTime_;
    double migrationTimeout_;

    // State management
    unsigned int requestCounter_;
    std::map<unsigned int, StandByElement> standByList_;
    // PERFORMANCE IMPROVEMENT: Index for O(1) duplicate migration check by UE address
    std::unordered_map<std::string, unsigned int> standByUeIndex_;
    std::map<unsigned int, cMessage*> timeoutMessages_;  // Track scheduled timeouts

    // Private helper methods
    cModule* findMecHostByName(const std::string& mehName);
    bool validateTargetMEH(cModule* targetMEH, const ApplicationDescriptor& appDesc);
    MigrationResult performMigration(int contextId, cModule* targetMEH,
                                      const std::string& ueAddress,
                                      const ApplicationDescriptor& appDesc);
    void sendMehChangeRequest(std::string ueIp, std::string newMehAddress,
                              int newPort, unsigned int requestNumber);
    void scheduleTimeout(unsigned int requestNumber);
    void cancelTimeout(unsigned int requestNumber);
    void forceCompleteMigration(unsigned int requestNumber, const std::string& reason);

    /*
     * Closes out a migration in the decision log: the row a strategy wrote when it
     * started this one carries the same requestNumber, and joining the two is what
     * turns "a migration was started" into "a migration finished, and how".
     *
     * oldInstanceTerminated is carried because a migration that leaves its old
     * instance running is a leak that costs resources for the rest of the run, and
     * it is invisible everywhere else.
     */
    void recordMigrationOutcome(const StandByElement& standBy, DecisionOutcome outcome,
                                bool oldInstanceTerminated, const std::string& reason);

    /*
     * Destroys the migrated-from instance once the UE has switched away from it.
     *
     * Goes to the old host's platform manager directly rather than through
     * MecAppLifecycleManager::stopApplication(), which cannot do this job: it looks the
     * app up by contextId, and by this point the registry entry already points at the
     * new instance on the new host — a contextId lookup would terminate the instance
     * the UE just switched *to*.
     *
     * StandByElement carries oldMecpm and mecUeAppID for exactly this, captured before
     * the registry entry was repointed.
     */
    bool terminateOldInstance(const StandByElement& standBy);
};

} 

#endif