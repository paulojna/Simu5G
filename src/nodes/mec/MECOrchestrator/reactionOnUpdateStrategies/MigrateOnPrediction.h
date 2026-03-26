#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_

#include "ReactionOnUpdate.h"
#include <unordered_map>
#include <fstream>

namespace simu5g {

/**
 * MigrateOnPrediction Strategy
 *
 * This strategy handles proactive migrations based on ML predictions from
 * an external Flask server. It works in conjunction with the
 * SendToExternalServer policy on the RavensController side.
 *
 * For USERS_UPDATE (reactive path):
 *   - Exit (newMEHId empty) → remove app + cancel any scheduled prediction
 *   - Entry (lastMEHId empty) → check if migration is needed
 *   (No handovers — in SendToExternalServer mode, handovers are Flask's job via MIGRATION_PLAN)
 *
 * For MIGRATION_PLAN (proactive path):
 *   - Schedule self-messages at predicted times
 *   - New prediction for same UE replaces the old one
 *   - Migration is timed so it COMPLETES at the predicted arrival time
 *
 * Only user EXIT cancels predictions. Reactive handovers do NOT cancel
 * predictions — this is intentional so we can measure the real impact
 * of ML-driven proactive migration.
 */
class MigrateOnPrediction : public ReactionOnUpdate
{
  private:
    omnetpp::cSimpleModule* owner_;   // MecOrchestrator, needed for scheduleAt/cancelAndDelete
    double migrationTime_;            // time it takes to complete a migration (to offset scheduling)

    // Map from UE address → scheduled self-message pointer
    std::unordered_map<std::string, omnetpp::cMessage*> scheduledPredictions_;

    // CSV log file for post-simulation analysis of migration types
    std::ofstream logFile_;

  public:
    MigrateOnPrediction(IOrchestratorApi* api, omnetpp::cSimpleModule* owner, double migrationTime);
    virtual ~MigrateOnPrediction();

    virtual void reactOnUpdate(const simu5g::UserMEHUpdate&) override;
    virtual void reactOnUpdate(const std::vector<simu5g::MigrationPrediction>&) override;
    virtual void handleScheduledEvent(omnetpp::cMessage* msg) override;
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONPREDICTION_H_ */
