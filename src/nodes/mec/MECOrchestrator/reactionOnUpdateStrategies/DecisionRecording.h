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
    decision.ueAddress = canonicalUeAddress(event.ueAddress);
    decision.trigger = DecisionTrigger::ConfirmedEvent;
    decision.observedAt = event.observedAt;
    decision.fromMEHId = event.fromMEHId;
    decision.toMEHId = event.toMEHId;
    return decision;
}

// fillFromMigrationResult, which turns a MigrationResult into the decision's
// kind and outcome, lives next to MigrationResult in MecAppMigrationManager.h —
// the migration manager records outcomes of its own and cannot include this
// header without a cycle.

} // namespace simu5g

#endif /* NODES_MEC_MECORCHESTRATOR_DECISIONRECORDING_H_ */
