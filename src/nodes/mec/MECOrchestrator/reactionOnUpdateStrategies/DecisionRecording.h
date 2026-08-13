#ifndef NODES_MEC_MECORCHESTRATOR_DECISIONRECORDING_H_
#define NODES_MEC_MECORCHESTRATOR_DECISIONRECORDING_H_

#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/UserEvent.h"
#include "nodes/mec/MECOrchestrator/services/DecisionLogger/OrchestrationDecision.h"
#include "nodes/mec/MECOrchestrator/services/MecAppMigrationManager/MecAppMigrationManager.h"

namespace simu5g {

// Shared by every strategy that records decisions, so that the same situation
// produces the same row whichever strategy was running. The modes are compared
// on these rows; a strategy phrasing "nothing was needed" its own way would show
// up in the comparison as a difference in behaviour.

// Everything a record takes from the event that triggered it. The caller fills
// in what it decided.
inline OrchestrationDecision decisionFromEvent(const UserEvent& event)
{
    OrchestrationDecision decision;
    decision.decidedAt = omnetpp::simTime();
    decision.ueAddress = event.ueAddress;
    decision.trigger = DecisionTrigger::ConfirmedEvent;
    decision.observedAt = event.observedAt;
    decision.fromMEHId = event.fromMEHId;
    decision.toMEHId = event.toMEHId;
    return decision;
}

// The three answers a migration request can give, in the log's vocabulary:
// started, nothing was called for, or it was called for and failed.
//
// A queued migration counts as neither started nor unnecessary, so it lands in
// Failed carrying its own reason — which is the honest reading of it today: the
// move was called for and did not happen. See the queued-migrations section of
// meo-plan.md.
inline void fillFromMigrationResult(OrchestrationDecision& decision, const MigrationResult& result)
{
    if (result.success) {
        decision.kind = DecisionKind::Migrate;
        decision.outcome = DecisionOutcome::Initiated;
        decision.requestNumber = result.requestNumber;
    }
    else if (result.nothingToDo) {
        decision.kind = DecisionKind::None;
        decision.outcome = DecisionOutcome::NotNeeded;
        decision.reason = result.errorMessage;
    }
    else {
        decision.kind = DecisionKind::Migrate;
        decision.outcome = DecisionOutcome::Failed;
        decision.reason = result.errorMessage;
    }
}

} // namespace simu5g

#endif /* NODES_MEC_MECORCHESTRATOR_DECISIONRECORDING_H_ */
