#ifndef NODES_MEC_MECORCHESTRATOR_INTERFACES_IORCHESTRATIONAPI_H_
#define NODES_MEC_MECORCHESTRATOR_INTERFACES_IORCHESTRATIONAPI_H_

#include <string>
#include <vector>
#include "nodes/mec/utils/httpUtils/json.hpp"
#include "apps/mec/RavensApps/RavensControllerUpdatePacket_m.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"



namespace simu5g {

class IOrchestratorApi {
    public:
        virtual void removeAppFromSystem(std::string ueAddress, std::string oldMEHId) = 0;
        virtual void migrateApp(std::string ueAddress, std::string newMEHId, std::string oldMEHId) = 0;
        virtual void checkIfMigrationIsNeeded(std::string ueAddress, std::string newMEHId, std::string oldMEHId) = 0;
        virtual nlohmann::json formatDataFromRAVENS(std::vector<UserEntryUpdate> UserEntryUpdatedList) = 0;
        virtual std::string postRequestPrediction(const std::string &url, const nlohmann::json &jsonObject) = 0;
        virtual ~IOrchestratorApi() = default;
};

}

#endif