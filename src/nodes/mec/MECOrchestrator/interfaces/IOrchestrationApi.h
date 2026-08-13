#ifndef NODES_MEC_MECORCHESTRATOR_INTERFACES_IORCHESTRATIONAPI_H_
#define NODES_MEC_MECORCHESTRATOR_INTERFACES_IORCHESTRATIONAPI_H_

#include <string>
#include <vector>
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/MECOrchestrator/services/DecisionLogger/OrchestrationDecision.h"

namespace simu5g {

struct MigrationResult;
class UALCMPMessage;

class IOrchestratorApi {
    public:
        virtual void removeAppFromSystem(std::string ueAddress, std::string oldMEHId) = 0;

        virtual MigrationResult migrateApp(std::string ueAddress, std::string newMEHId, std::string oldMEHId) = 0;
        virtual MigrationResult checkIfMigrationIsNeeded(std::string ueAddress, std::string newMEHId, std::string oldMEHId) = 0;
        virtual MigrationResult completeMigration(UALCMPMessage* ackMsg) = 0;

        virtual std::string getAppCurrentMEH(std::string ueAddress) = 0;

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