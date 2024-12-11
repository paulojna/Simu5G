#include "MigrateOnTime.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"
#include <cmath>

namespace simu5g {

void MigrateOnTime::reactOnUpdate(const std::vector<UserEntryUpdate> &updatedList)
{
    nlohmann::json jsonObjectToSend = mecOrchestrator_->formatDataFromRAVENS(updatedList);
    std::string response = mecOrchestrator_->postRequestPrediction(url, jsonObjectToSend);
    //std::cout << simTime() << " - MecOrchestrator::POST_RESPONSE_FROM_FLASK_SERVER - response: " << response << endl;

    // we are going to implement a scheduling mechanism to trigger the migration on the MEO itself for each user
    // first we need to parse the response and run through the list of users
    nlohmann::json jsonResponse = nlohmann::json::parse(response);
    // std::cout << simTime() << " - MigrateOnTime::reactOnUpdate - response received from the prediction server: " << jsonResponse.dump() << std::endl;
    // run through the list of objects received in the response
    for (auto &user : jsonResponse)
    {
        int nextMEHId_ = user["NextAccessPointId"].get<int>();
        int currentMEHId_ = user["AccessPointId"].get<int>();
        std::string nextMEHId = "mecHost" + std::to_string(nextMEHId_);
        std::string currentMEHId = "mecHost" + std::to_string(currentMEHId_);
        // std::cout << simTime() << " - MigrateOnTime::reactOnUpdate - user address: " << user["Address"] << " nextMEHId: " << nextMEHId << " currentMEHId: " << currentMEHId << std::endl;
        // schedule self message of type MigrateAppMessage to trigger the migration
        if(user["NextAccessPointId"] == -1)
        {
            // remove the user from the MEC system
            MigrateAppMessage *migrateMsg = new MigrateAppMessage("MigrateAppMessage");
            migrateMsg->setType(0);
            migrateMsg->setUeAddress(user["Address"].get<std::string>().c_str());
            migrateMsg->setNewMEHId(nextMEHId.c_str());
            migrateMsg->setOldMEHId(currentMEHId.c_str());
            double migrationTime = user["Duration"].get<double>() - mecOrchestrator_->getMigrationTime() + 30;
            mecOrchestrator_->scheduleAt(simTime() + simtime_t(migrationTime), migrateMsg);
        }
        else
        {
            // schedule the migration
            MigrateAppMessage *migrateMsg = new MigrateAppMessage("MigrateAppMessage");
            migrateMsg->setType(1);
            migrateMsg->setUeAddress(user["Address"].get<std::string>().c_str());
            migrateMsg->setNewMEHId(nextMEHId.c_str());
            migrateMsg->setOldMEHId(currentMEHId.c_str());
            double migrationTime = user["Duration"].get<double>() - mecOrchestrator_->getMigrationTime();
            mecOrchestrator_->scheduleAt(simTime() + simtime_t(migrationTime), migrateMsg);
        }
    }

}

void MigrateOnTime::reactOnUpdate(const UserMEHUpdate &update)
{
    // not implemented
}

}
