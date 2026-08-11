#include "RemoveOnExit.h"
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
    api_->removeAppFromSystem(event.ueAddress, event.fromMEHId);
}

}
