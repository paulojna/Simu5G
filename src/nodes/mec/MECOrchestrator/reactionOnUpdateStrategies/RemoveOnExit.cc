#include "RemoveOnExit.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"

// goal is to remove the MEC app when the UE gets out of the system/scenario

namespace simu5g {

void RemoveOnExit::reactOnUpdate(const std::vector<UserEntryUpdate> &updatedList)
{
    // not implemented
}

void RemoveOnExit::reactOnUpdate(const UserMEHUpdate &update)
{
    // check it the newMEH is empty
    if (update.getNewMEHId()==" ")
    {
        EV << "RemoveOnExit::reactOnUpdate - newMEHId is empty - it got out of the system -> removing MEC app" << endl;
        // remove the acr: part of the address and convertit to inet::L3Address
        std::string ueAddress = update.getAddress();
        std::string ueIp = ueAddress.substr(4);
        inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

        // remove user from the userMEHMap
        mecOrchestrator_->userMEHMap.erase(ueAddress);

        // requestId will be zero so the UALCMP will not try to send a response to the UE when receives the ack from the MEO
        int requestId = 0; // I've changed the starting value of the request counter to 1

        // find the contextId that has the same ueAddress
        int contextId = -1;
        for (auto &it : mecOrchestrator_->meAppMap)
        {
            if (it.second.ueAddress == ueL3Address)
            {
                EV << "RemoveOnExit::reactOnUpdate - contextId found for ueAddress " << ueAddress << " is " << it.first << endl;
                contextId = it.first;
                break;
            }
        }

        if (contextId == -1)
        {
            EV << "RemoveOnExit::reactOnUpdate - ERROR: contextId not found for ueAddress " << ueAddress << endl;
            return;
        }

        // create the message to stop the MEC app
        DeleteContextAppMessage *msg = new DeleteContextAppMessage("DeleteContextAppMessage");
        msg->setType(DELETE_CONTEXT_APP);
        msg->setRequestId(requestId);
        msg->setContextId(contextId);

        // invoke stopMECApp method from MecOrchestrator
        EV << "RemoveOnExit::reactOnUpdate - sending DeleteContextAppMessage to MecOrchestrator to stop MEC app with contextId " << contextId << endl;
        mecOrchestrator_->stopMECApp(msg);
    }
    else
    {
        EV << "RemoveOnExit::reactOnUpdate - newMEHId is not empty, no action required" << endl;
    }
}

}