#ifndef NODES_MEC_MECORCHESTRATOR_INTERFACES_IORCHESTRATIONAPI_H_
#define NODES_MEC_MECORCHESTRATOR_INTERFACES_IORCHESTRATIONAPI_H_

#include <string>
#include <vector>
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/MECOrchestrator/services/DecisionLogger/OrchestrationDecision.h"
#include "nodes/mec/MECOrchestrator/services/MecAppRegistry/MecAppRegistry.h"

namespace simu5g {

struct MigrationResult;
class UALCMPMessage;

// One row of the application view, as a strategy is allowed to see it: where the
// application is and what state it is in, and nothing else.
//
// Deliberately not the registry's AppEntry. That entry carries module pointers,
// endpoints and sequence numbers, which are lifecycle bookkeeping — handing them
// to a strategy would make "strategies read, never write" a convention rather
// than something the interface enforces.
struct AppPlacement {
    std::string ueAddress;   // canonical form: bare IP, no "acr:" prefix
    std::string mecHost;     // empty only if the entry has lost its host module
    MecAppRegistry::AppState state;
    int contextId;
};

// One row of the user view, as a strategy is allowed to see it.
//
// A separate type from AppPlacement, and returned by a separate call, on
// purpose: the two views answer different questions and are allowed to
// disagree — during a proactive migration the user is still on the old host
// while the application is already on the new one. A single row type carrying
// both would invite exactly the reconciliation the split exists to prevent.
struct UserPresenceRow {
    std::string ueAddress;         // canonical form: bare IP
    std::string currentMEH;        // empty = the user has exited
    omnetpp::simtime_t lastEventAt = -1;
    std::string lastObservedMEH;   // telemetry enrichment; empty when telemetry is off
    omnetpp::simtime_t lastSampleAt = -1;
};

class IOrchestratorApi {
    public:
        virtual void removeAppFromSystem(std::string ueAddress, std::string oldMEHId) = 0;

        virtual MigrationResult migrateApp(std::string ueAddress, std::string newMEHId, std::string oldMEHId) = 0;
        virtual MigrationResult checkIfMigrationIsNeeded(std::string ueAddress, std::string newMEHId, std::string oldMEHId) = 0;
        virtual MigrationResult completeMigration(UALCMPMessage* ackMsg) = 0;

        virtual std::string getAppCurrentMEH(std::string ueAddress) = 0;

        // The two views, whole, as copies. The event-driven strategies ask about
        // one user at a time through getAppCurrentMEH, which is all a per-event
        // decision needs; the learning strategy has to send the engine the state
        // of everything at once, and asking per user would first require a list
        // of users — which is the other view, and just as unreachable.
        //
        // Copies rather than references because strategies read and never write.
        // The selection policies reach the views directly through friendship;
        // strategies deliberately do not, so the only way in is a value that
        // cannot be written back.
        //
        // getAppPlacements carries Gone rows rather than filtering them: an
        // application that was deleted is a fact about that user, and dropping
        // the row would make it indistinguishable from a user who never had one.
        virtual std::vector<AppPlacement> getAppPlacements() = 0;
        virtual std::vector<UserPresenceRow> getUserPresence() = 0;

        // Writes one row to the decision log. Every strategy records what it
        // decided through here, including deciding to do nothing: the modes are
        // compared on their decisions, so the record has to come out the same
        // shape whichever strategy produced it. A no-op when no log path is
        // configured.
        virtual void recordDecision(const OrchestrationDecision& decision) = 0;

        virtual ~IOrchestratorApi() = default;
};

}

#endif