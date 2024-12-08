#include "MigrateOnChange.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"

/* 
    This strategy will react to three different combinations of messages from the RAVENS Controller:
        1) newMEHId is empty -> the UE got out of the system
        2) newMEHId and lastMEHId are not empty -> the UE is migrating from one MEH to another
        3) lastMEHId is empty -> new user detected by RAVENS (in this case we have to deal with the situation where the UE is already being served by a MEC app)
*/

namespace simu5g {

void MigrateOnChange::reactOnUpdate(const std::vector<UserEntryUpdate> &updatedList)
{
    // not implemented
}

void MigrateOnChange::reactOnUpdate(const UserMEHUpdate &update)
{
    // check if the newMEH is empty
    if (update.getNewMEHId()==" ")
    {
        EV << "MigrateOnChange::reactOnUpdate - newMEHId is not empty - it got out of the system -> removing MEC app" << endl;
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
                EV << "MigrateOnChange::reactOnUpdate - contextId found for ueAddress " << ueAddress << " is " << it.first << endl;
                contextId = it.first;
                break;
            }
        }

        if (contextId == -1)
        {
            EV << "MigrateOnChange::reactOnUpdate - ERROR: contextId not found for ueAddress " << ueAddress << endl;
            return;
        }

        // create the message to stop the MEC app
        DeleteContextAppMessage *msg = new DeleteContextAppMessage("DeleteContextAppMessage");
        msg->setType(DELETE_CONTEXT_APP);
        msg->setRequestId(requestId);
        msg->setContextId(contextId);

        // invoke stopMECApp method from MecOrchestrator
        EV << "MigrateOnChange::reactOnUpdate - sending DeleteContextAppMessage to MecOrchestrator to stop MEC app with contextId " << contextId << endl;
        std::cout << "RemoveOnExit" << endl;
        mecOrchestrator_->stopMECApp(msg);
    }
    else if(update.getNewMEHId()!=" " && update.getLastMEHId()!=" " && update.getNewMEHId()!=update.getLastMEHId())
    {
        EV << "MigrateOnChange::reactOnUpdate - newMEHId and lastMEHId are not empty -> starting migration" << endl;
        // remove the acr: part of the address and convertit to inet::L3Address
        std::string ueAddress = update.getAddress();
        std::string ueIp = ueAddress.substr(4);
        inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

        std::string newMEHId = update.getNewMEHId();
        std::string oldMEHId = update.getLastMEHId();

        // first we should check if the MEH that has the newMEHId has enought resources, otherwise we should not start the migration

        /* Migration includes:
            1. Start the MEC app on the new MEH -> DONE
            2. Inform the UE about the new MEH (through the UALCMP) -> To be done here
            3. Add the UE to a standby list until the ACK from the UE is received -> DONE 
            4. Wait for the ACK from the UE (through the UALCMP)-> To be done on the mecOrchestrator
            5. Stop the MEC app on the old MEH -> To be done on the mecOrchestrator
        */ 

        // 1. Start the MEC app on the new MEH
        // 1.1 find the contextId that has the same ueAddress
        int contextId = -1;
        for (auto &it : mecOrchestrator_->meAppMap)
        {
            if (it.second.ueAddress == ueL3Address)
            {
                EV << "MigrateOnChange::reactOnUpdate - contextId found for ueAddress " << ueAddress << " is " << it.first << endl;
                contextId = it.first;
                break;
            }
        }

        if (contextId == -1)
        {
            EV << "MigrateOnChange::reactOnUpdate - ERROR: contextId not found for ueAddress " << ueAddress << endl;
            return;
        }
        else
        {
            // 1.2 get the meAppMap entry and get the App Descriptor associated with it through the appDId
            auto meAppMapEntry = mecOrchestrator_->meAppMap.find(contextId);
            const ApplicationDescriptor& desc = mecOrchestrator_->mecApplicationDescriptors_.at(meAppMapEntry->second.appDId);

            // 1.3 add the standby information to the standByList
            standByElement standBy;
            standBy.mecUeAppID = meAppMapEntry->second.mecUeAppID;
            standBy.mecpm = meAppMapEntry->second.mecpm; 

            // 1.3 we should check if the MEH with the newMEHId has enought resources, otherwise we should not start the migration
            // we should also check if the MEH with the newMEHId has the required MEC services, otherwise we should not start the migration
            cModule *newMEH = nullptr;
            for (auto &it : mecOrchestrator_->mecHosts)
            {
                if (it->getName() == newMEHId)
                {
                    EV << "MigrateOnChange::reactOnUpdate - newMEH found on mecHosts list" << endl;
                    newMEH = it;
                    break;
                }
            }

            if (newMEH == nullptr)
            {
                EV << "MigrateOnChange::reactOnUpdate - ERROR: newMEH not found on mecHosts list" << endl;
                return;
            }

            // check if the newMEH has enought resources
            VirtualisationInfrastructureManager *vim = check_and_cast<VirtualisationInfrastructureManager*> (newMEH->getSubmodule("vim"));
            ResourceDescriptor resources = desc.getVirtualResources();
            bool res = vim->isAllocable(resources.ram, resources.disk, resources.cpu);
            
            if(!res)
            {   
                EV << "MigrateOnChange::reactOnUpdate - ERROR: newMEH does not have enought resources" << endl;
                return;
            }   

            // 1.4 create the message to start the MEC app on the new MEH
            CreateAppMessage *msg = new CreateAppMessage();
            msg->setUeAppID(standBy.mecUeAppID);
            msg->setMEModuleName(desc.getAppName().c_str());
            msg->setMEModuleType(desc.getAppProvider().c_str());

            msg->setRequiredCpu(desc.getVirtualResources().cpu);
            msg->setRequiredRam(desc.getVirtualResources().ram);
            msg->setRequiredDisk(desc.getVirtualResources().disk);

            if(!desc.getOmnetppServiceRequired().empty())
                msg->setRequiredService(desc.getOmnetppServiceRequired().c_str());
            else
                msg->setRequiredService("NULL");
            msg->setContextId(meAppMapEntry->first);

            // change de mecapp in the map structure
            meAppMapEntry->second.mecHost = newMEH;
            meAppMapEntry->second.vim = vim;
            meAppMapEntry->second.mecpm = newMEH->getSubmodule("mecPlatformManager");
            meAppMapEntry->second.appDId = desc.getAppDId();
            meAppMapEntry->second.mecUeAppID = standBy.mecUeAppID;
            meAppMapEntry->second.ueAddress = ueL3Address;

            MecPlatformManager* mecpm = check_and_cast<MecPlatformManager*>(meAppMapEntry->second.mecpm);

            MecAppInstanceInfo* appInfo = nullptr;
            appInfo = mecpm->instantiateMEApp(msg);   

            if(!appInfo->status)
            {
                EV << "MigrateOnChange::reactOnUpdate - ERROR: MEC app could not be instantiated on the newMEH" << endl;
                return;
            }

            EV << "MigrateOnChange::reactOnUpdate - new MEC application with name: " << appInfo->instanceId << " instantiated on MEC host []"<< meAppMapEntry->second.mecHost << " at "<< appInfo->endPoint.addr.str() << ":" << appInfo->endPoint.port << endl;

            meAppMapEntry->second.mecAppAddress = appInfo->endPoint.addr;
            meAppMapEntry->second.mecAppPort = appInfo->endPoint.port;
            meAppMapEntry->second.mecAppIsntanceId = appInfo->instanceId;

            //update the meAppMap
            mecOrchestrator_->meAppMap[meAppMapEntry->first] = meAppMapEntry->second;

            standBy.request = mecOrchestrator_->requestCounter;
            mecOrchestrator_->requestCounter++; 

            // 2. Inform the UE about the new MEH (through the UALCMP)
            mecOrchestrator_->sendMehChangeRequest(ueIp, appInfo->endPoint.addr.str(), appInfo->endPoint.port, standBy.request);

            // 3. Add the UE to a standby list until the ACK from the UE is received
            mecOrchestrator_->standByList[standBy.request] = standBy; 

            delete appInfo;

            return;
        }   
    }
    else if (update.getLastMEHId()==" " && update.getNewMEHId()!=" ")
    {
        EV << "MigrateOnChange::reactOnUpdate - lastMEHId is empty, new user detected by RAVENS. We need to check if migration is needed!" << endl;
        /*
            This process will include:
            1. Check if the UE has already a MEC app instantiated on the MEH with newMEHId
            2. If not: 
                3.1 and if it is not instantiated on any MEH, do nothing and wait for the UE to ask for a MEC app
                3.2 and if it is instantiated on another MEH, migrate the instance to the MEH with newMEHId
            3. If it is already instantiated on the MEH with newMEHId, do nothing
        */

        // remove the acr: part of the address and convertit to inet::L3Address
        std::string ueAddress = update.getAddress();
        std::string ueIp = ueAddress.substr(4);
        inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

        std::string newMEHId = update.getNewMEHId();

        // 1. check if the UE has a mecAppMapEntry on the meAppMap and if so, check which MEH is serving it
        int contextId = -1;
        for (auto &it : mecOrchestrator_->meAppMap)
        {
            if (it.second.ueAddress == ueL3Address)
            {
                EV << "MigrateOnChange::reactOnUpdate - contextId found for ueAddress " << ueAddress << " is " << it.first << endl;
                contextId = it.first;
                break;
            }
        }

        if (contextId == -1)
        {
            // UE does not have the MEC App instantiated in any MEH. We should wait for a UE Request.
            EV << "MigrateOnChange::reactOnUpdate - WARNING: contextId not found for ueAddress! Nothing to do!" << ueAddress << endl;
            return;
        }
        else
        {
            // UE already has the MEC App instantiated in one MEH -> but in which?
            auto meAppMapEntry = mecOrchestrator_->meAppMap.find(contextId);
            if (meAppMapEntry->second.mecHost->getName() == newMEHId)
            {
                // UE is already being served by the newMEHId -> nothing to do
                EV << "MigrateOnChange::reactOnUpdate - UE is already being served by the " << newMEHId << " -> nothing to do" << endl;
                return;
            }
            else
            {
                // UE is under a different MEH -> we should migrate the instance to the newMEHId
                EV << "MigrateOnChange::reactOnUpdate - UE is in a different MEH -> we should migrate the instance to the " << newMEHId << " " << endl;
                const ApplicationDescriptor& desc = mecOrchestrator_->mecApplicationDescriptors_.at(meAppMapEntry->second.appDId);

                standByElement standBy;
                standBy.mecUeAppID = meAppMapEntry->second.mecUeAppID;
                standBy.mecpm = meAppMapEntry->second.mecpm; 

                cModule *newMEH = nullptr;
                for (auto &it : mecOrchestrator_->mecHosts)
                {
                    if (it->getName() == newMEHId)
                    {
                        EV << "MigrateOnChange::reactOnUpdate - newMEH found on mecHosts list" << endl;
                        newMEH = it;
                        break;
                    }
                }

                if (newMEH == nullptr)
                {
                    EV << "MigrateOnChange::reactOnUpdate - ERROR: newMEH not found on mecHosts list" << endl;
                    return;
                }

                VirtualisationInfrastructureManager *vim = check_and_cast<VirtualisationInfrastructureManager*> (newMEH->getSubmodule("vim"));
                ResourceDescriptor resources = desc.getVirtualResources();
                bool res = vim->isAllocable(resources.ram, resources.disk, resources.cpu);

                if(!res)
                {   
                    EV << "MigrateOnChange::reactOnUpdate - ERROR: newMEH does not have enought resources" << endl;
                    return;
                }

                CreateAppMessage *msg = new CreateAppMessage();
                msg->setUeAppID(standBy.mecUeAppID);
                msg->setMEModuleName(desc.getAppName().c_str());
                msg->setMEModuleType(desc.getAppProvider().c_str());

                msg->setRequiredCpu(desc.getVirtualResources().cpu);
                msg->setRequiredRam(desc.getVirtualResources().ram);
                msg->setRequiredDisk(desc.getVirtualResources().disk);

                if(!desc.getOmnetppServiceRequired().empty())
                    msg->setRequiredService(desc.getOmnetppServiceRequired().c_str());
                else
                    msg->setRequiredService("NULL");
                msg->setContextId(meAppMapEntry->first);   

                // change de mecapp in the map structure
                meAppMapEntry->second.mecHost = newMEH;
                meAppMapEntry->second.vim = vim;
                meAppMapEntry->second.mecpm = newMEH->getSubmodule("mecPlatformManager");
                meAppMapEntry->second.appDId = desc.getAppDId();
                meAppMapEntry->second.mecUeAppID = standBy.mecUeAppID;
                meAppMapEntry->second.ueAddress = ueL3Address;

                MecPlatformManager* mecpm = check_and_cast<MecPlatformManager*>(meAppMapEntry->second.mecpm);

                MecAppInstanceInfo* appInfo = nullptr;
                appInfo = mecpm->instantiateMEApp(msg);   

                if(!appInfo->status)
                {
                    EV << "MigrateOnChange::reactOnUpdate - ERROR: MEC app could not be instantiated on the newMEH" << endl;
                    return;
                }

                EV << "MigrateOnChange::reactOnUpdate - new MEC application with name: " << appInfo->instanceId << " instantiated on MEC host []"<< meAppMapEntry->second.mecHost << " at "<< appInfo->endPoint.addr.str() << ":" << appInfo->endPoint.port << endl;

                meAppMapEntry->second.mecAppAddress = appInfo->endPoint.addr;
                meAppMapEntry->second.mecAppPort = appInfo->endPoint.port;
                meAppMapEntry->second.mecAppIsntanceId = appInfo->instanceId;

                //update the meAppMap
                mecOrchestrator_->meAppMap[meAppMapEntry->first] = meAppMapEntry->second;

                standBy.request = mecOrchestrator_->requestCounter;
                mecOrchestrator_->requestCounter++; 

                // 2. Inform the UE about the new MEH (through the UALCMP)
                mecOrchestrator_->sendMehChangeRequest(ueIp, appInfo->endPoint.addr.str(), appInfo->endPoint.port, standBy.request);

                // 3. Add the UE to a standby list until the ACK from the UE is received
                mecOrchestrator_->standByList[standBy.request] = standBy; 

                delete appInfo;

                return;
            }
        }
    }
    else
    {
        EV << "MigrateOnChange::reactOnUpdate - ERROR: update fields not recognized!" << endl;
    }
}

}