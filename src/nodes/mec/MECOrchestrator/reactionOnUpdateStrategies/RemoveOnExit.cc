#include "RemoveOnExit.h"
#include "DecisionRecording.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"

// goal is to remove the MEC app when the UE gets out of the system/scenario

namespace simu5g {

void RemoveOnExit::reactOnUpdate(const UserEvent &event)
{
    if (event.eventType != USER_EXIT)
        return;

    EV << "RemoveOnExit::reactOnUpdate - " << event.ueAddress << " left " << event.fromMEHId
       << ", removing its app" << endl;

    // Asked before the removal, because afterwards there is no way to tell a
    // user whose application was deleted from one that never had one.
    bool hadApp = !api_->getAppCurrentMEH(event.ueAddress).empty();

    api_->removeAppFromSystem(event.ueAddress, event.fromMEHId);

    OrchestrationDecision decision = decisionFromEvent(event);
    decision.kind = hadApp ? DecisionKind::Remove : DecisionKind::None;
    decision.outcome = hadApp ? DecisionOutcome::Success : DecisionOutcome::NotNeeded;
    if (!hadApp)
        decision.reason = "no application to remove";
    api_->recordDecision(decision);
}

}
