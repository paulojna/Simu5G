#include "MigrateOnChange.h"
#include "DecisionRecording.h"
#include "nodes/mec/MECOrchestrator/services/MecAppMigrationManager/MecAppMigrationManager.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"

/*
 * MigrateOnChange Strategy
 *
 * Reacts to confirmed changes in where a user is, one event at a time:
 *
 *   EXIT     -> the user left the system, so remove its app
 *   HANDOVER -> the user moved between hosts, so migrate its app after it
 *   ENTRY    -> first sight of the user; it may have no app yet, or already have
 *               one in the right place, so ask whether a migration is needed at
 *               all rather than assuming
 *
 * This used to be the same three cases inferred from which of two host-name
 * strings happened to be empty, with a fourth "unexpected update" branch for the
 * combinations that inference could not account for. The event now says which
 * one it is, so there is nothing left to infer and no unexpected case.
 */

namespace simu5g {

using namespace omnetpp;

void MigrateOnChange::reactOnUpdate(const UserEvent &event)
{
    switch (event.eventType) {

    case USER_EXIT: {
        EV << "MigrateOnChange::reactOnUpdate - UE left system!" << endl;
        EV << "  UE: " << event.ueAddress << endl;
        EV << "  Last MEH: " << event.fromMEHId << endl;

        // Asked before the removal: afterwards there is no telling a user whose
        // application was deleted from one that never had one.
        bool hadApp = !api_->getAppCurrentMEH(event.ueAddress).empty();

        api_->removeAppFromSystem(event.ueAddress, event.fromMEHId);

        OrchestrationDecision decision = decisionFromEvent(event);
        decision.kind = hadApp ? DecisionKind::Remove : DecisionKind::None;
        decision.outcome = hadApp ? DecisionOutcome::Success : DecisionOutcome::NotNeeded;
        if (!hadApp)
            decision.reason = "no application to remove";
        api_->recordDecision(decision);
        break;
    }

    case USER_HANDOVER: {
        EV << "MigrateOnChange::reactOnUpdate - Migration between MEHs detected" << endl;
        EV << "  UE: " << event.ueAddress << endl;
        EV << "  From: " << event.fromMEHId << " To: " << event.toMEHId << endl;

        MigrationResult result = api_->migrateApp(event.ueAddress, event.toMEHId, event.fromMEHId);

        if (!result.success)
        {
            EV << "MigrateOnChange::reactOnUpdate - Migration failed: " << result.errorMessage << endl;
            // Migration failed - UE continues using old MEH endpoint.
            // Deliberately nothing else: the old instance never stopped serving,
            // and the failure is now in the decision log. See item 3 of meo-plan.md.
        }
        else
        {
            EV << "MigrateOnChange::reactOnUpdate - Migration initiated successfully" << endl;
            EV << "  Request Number: " << result.requestNumber << endl;
            EV << "  Context ID: " << result.contextId << endl;
        }

        OrchestrationDecision decision = decisionFromEvent(event);
        fillFromMigrationResult(decision, result);
        api_->recordDecision(decision);
        break;
    }

    case USER_ENTRY: {
        EV << "MigrateOnChange::reactOnUpdate - New UE detected by RAVENS" << endl;
        EV << "  UE: " << event.ueAddress << endl;
        EV << "  Target MEH: " << event.toMEHId << endl;

        // This method handles three sub-cases:
        // - UE has no app yet → Returns "No migration needed"
        // - UE already on target MEH → Returns "No migration needed"
        // - UE on different MEH → Triggers migration
        MigrationResult result = api_->checkIfMigrationIsNeeded(event.ueAddress, event.toMEHId, event.fromMEHId);

        if (result.success) {
            EV << "MigrateOnChange::reactOnUpdate - Migration was needed and initiated" << endl;
            EV << "  Request Number: " << result.requestNumber << endl;
        } else {
            // Normal cases: no app instantiated yet, or already on correct MEH
            EV << "MigrateOnChange::reactOnUpdate - " << result.errorMessage << endl;
        }

        OrchestrationDecision decision = decisionFromEvent(event);
        fillFromMigrationResult(decision, result);
        api_->recordDecision(decision);
        break;
    }

    default:
        EV << "MigrateOnChange::reactOnUpdate - unknown event type " << event.eventType
           << " for UE " << event.ueAddress << ", ignoring" << endl;
        break;
    }
}

}
