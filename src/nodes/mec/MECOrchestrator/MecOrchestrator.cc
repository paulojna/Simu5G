//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//

#include "nodes/mec/MECOrchestrator/MecOrchestrator.h"

#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

#include "nodes/mec/MECPlatform/ServiceRegistry/ServiceRegistry.h"

#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"

#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"
#include "nodes/mec/UALCMP/UALCMPMessages/CreateContextAppMessage.h"
#include "nodes/mec/UALCMP/UALCMPMessages/CreateContextAppAckMessage.h"


#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/MecServiceSelectionBased.h"
#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/AvailableResourcesSelectionBased.h"
#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/MecHostSelectionBased.h"
#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/LocationSelectionBased.h"

#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/RemoveOnExit.h"
#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/MigrateOnChange.h"
#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/MigrateOnTime.h"

#include "apps/mec/RavensApps/RavensControllerUpdatePacket_m.h"

//emulation debug
#include <iostream>
#include <curl/curl.h>

namespace simu5g {

#define USERS_UPDATE 7
#define USERS_ENTRY 8

Define_Module(MecOrchestrator);

MecOrchestrator::MecOrchestrator()
{
    meAppMap.clear();
    mecApplicationDescriptors_.clear();
    mecHostSelectionPolicy_ = nullptr;
    userMEHMap.clear();
    reactionOnUpdate_ = nullptr;
}

void MecOrchestrator::initialize(int stage)
{
    cSimpleModule::initialize(stage);
    // avoid multiple initializations
    if (stage!=inet::INITSTAGE_LOCAL)
        return;
    EV << "MecOrchestrator::initialize - stage " << stage << endl;

    binder_ = getBinder();

    if(!strcmp(par("selectionPolicy"), "MecServiceBased"))
        mecHostSelectionPolicy_ = new MecServiceSelectionBased(this);
    else if(!strcmp(par("selectionPolicy"), "AvailableResourcesBased"))
        mecHostSelectionPolicy_ = new AvailableResourcesSelectionBased(this);
    else if(!strcmp(par("selectionPolicy"), "MecHostBased"))
            mecHostSelectionPolicy_ = new MecHostSelectionBased(this, par("mecHostIndex"));
    else if(!strcmp(par("selectionPolicy"), "LocationBased"))
        mecHostSelectionPolicy_ = new LocationSelectionBased(this);
    else
        throw cRuntimeError("MecOrchestrator::initialize - Selection policy %s not present!" , par("selectionPolicy").stringValue());

    if(!strcmp(par("reactionStrategy"), "RemoveOnExit"))
        reactionOnUpdate_ = new RemoveOnExit(this);
    else if(!strcmp(par("reactionStrategy"), "MigrateOnChange"))
        reactionOnUpdate_ = new MigrateOnChange(this);
    else if(!strcmp(par("reactionStrategy"), "MigrateOnTime"))
        reactionOnUpdate_ = new MigrateOnTime(this);
    else
        throw cRuntimeError("MecOrchestrator::initialize - Reaction on update strategy %s not present!" , par("reactionOnUpdateStrategy").stringValue());

    onboardingTime = par("onboardingTime").doubleValue();
    instantiationTime = par("instantiationTime").doubleValue();
    terminationTime = par("terminationTime").doubleValue();

    migrationTime = par("migrationTime").doubleValue();

    requestCounter = 0;

    getConnectedMecHosts();
    onboardApplicationPackages();
}

void MecOrchestrator::handleMessage(cMessage *msg)
{
    if (msg->isSelfMessage())
    {
        if(strcmp(msg->getName(), "MECOrchestratorMessage") == 0)
        {
            EV << "MecOrchestrator::handleMessage - "  << msg->getName() << endl;
            MECOrchestratorMessage* meoMsg = check_and_cast<MECOrchestratorMessage*>(msg);
            if(strcmp(meoMsg->getType(), CREATE_CONTEXT_APP) == 0)
            {
                if(meoMsg->getSuccess())
                    sendCreateAppContextAck(true, meoMsg->getRequestId(), meoMsg->getContextId());
                else
                    sendCreateAppContextAck(false, meoMsg->getRequestId());
            }
            else if( strcmp(meoMsg->getType(), DELETE_CONTEXT_APP) == 0)
                sendDeleteAppContextAck(meoMsg->getSuccess(), meoMsg->getRequestId(), meoMsg->getContextId());
        }
        else if(strcmp(msg->getName(), "UpdateFromRAVENS") == 0)
        {
            EV << "I'm going to do something here" << endl;
        }
        else if(strcmp(msg->getName(), "MigrateAppMessage") == 0)
        {
            MigrateAppMessage* migrateMsg = check_and_cast<MigrateAppMessage*>(msg);
            if(migrateMsg->getType() == 0)
            {
                // remove the user from the MEC system
                //std::cout << "BE AWARE: We should remove the user" << migrateMsg->getUeAddress() << " from the MEC system" << std::endl;
                removeAppTime(migrateMsg->getUeAddress(), migrateMsg->getOldMEHId());
            }
            else
            {
                //std::cout << "BE AWARE: We should migrate the user" << migrateMsg->getUeAddress() << " from the MEC system" << std::endl;
                migrateAppTime(migrateMsg->getUeAddress(), migrateMsg->getNewMEHId(), migrateMsg->getOldMEHId());
            }
            //std::cout << "MecOrchestrator::MigrateOnTime message received with fields:" << migrateMsg->getUeAddress() << " " << migrateMsg->getNewMEHId() << " " << migrateMsg->getOldMEHId() << std::endl;
            //migrateAppTime(migrateMsg->getUeAddress(), migrateMsg->getNewMEHId(), migrateMsg->getOldMEHId());
        }
    }

    // handle message from the LCM proxy
    else if(msg->arrivedOn("fromUALCMP"))
    {
        EV << "MecOrchestrator::handleMessage - "  << msg->getName() << endl;
        handleUALCMPMessage(msg);
    }

    else if(msg->arrivedOn("fromRavensController"))
    {
        EV << "MecOrchestrator::handleMessage fromRavensController - "  << msg->getName() << endl;

        inet::Packet* packet = check_and_cast<inet::Packet*>(msg);
        auto received_packet = packet->peekAtFront<RavensControllerUpdatePacket>();
        std::vector<UserMEHUpdate> UserMEHUpdatedList_toPrint;
        if(received_packet->getType() == USERS_UPDATE){
            auto usersUpdate = packet->peekAtFront<UserMEHUpdatedListMessage>();
            UserMEHUpdatedList_toPrint = usersUpdate->getUeMehList();
            for(auto user : UserMEHUpdatedList_toPrint){
                EV << "MecOrchestrator::socketDataArrived - user address: " << user.getAddress() << " last MEH: " << user.getLastMEHId() << " new MEH: " << user.getNewMEHId() << endl;
                userMEHMap[user.getAddress()] = std::make_pair(user.getAddress(), user.getNewMEHId());
                reactionOnUpdate_->reactOnUpdate(user);
                EV << "MecOrchestrator::socketDataArrived - reactOnUpdate done!" << endl;
            }
        }else if(received_packet->getType() == USERS_ENTRY){
            auto usersEntry = packet->peekAtFront<UserEntryListMessage>();
            std::vector<UserEntryUpdate> UserEntryList_toPrint = usersEntry->getUeEntryList();
            reactionOnUpdate_->reactOnUpdate(UserEntryList_toPrint);
            for(auto user : UserEntryList_toPrint){
                userMEHMap[user.getAddress()] = std::make_pair(user.getAddress(), user.getCurrentMEHId());
            }
        }
    }

    delete msg;
    return;

}

nlohmann::json MecOrchestrator::formatDataFromRAVENS(std::vector<UserEntryUpdate> UserEntryUpdatedList){
    nlohmann::json jsonList;
    // lets create a list of json objects, each one representing a user
    for(auto user : UserEntryUpdatedList){
        nlohmann::json userJson;
        userJson["AccessPointId"] = user.getAccessPointId();
        userJson["x"] = user.getX();
        userJson["y"] = user.getY();
        userJson["Speed"] = user.getHSpeed();
        userJson["Bearing"] = user.getBearing();
        userJson["DistanceToAccessPoint"] = user.getDistanceToAp();
        userJson["TimeSpent"] = "0";
        userJson["GB"] = "0";
        userJson["BG"] = "0";
        userJson["NumberOfUsers"] = user.getNumberOfUsers();
        userJson["AvgSpeed"] = user.getAvgSpeed();
        userJson["NumberOfUsersLessSpeed"] = user.getNumberOfUsersLessSpeed();
        userJson["address_"] = user.getAddress();
        userJson["currentMEHId_"] = user.getCurrentMEHId();
        userJson["nextMEHId_"] = user.getNextMEHId();
        userJson["timestamp_"] = user.getTimestamp().str();
        userJson["ueId_"] = user.getUeId();
        jsonList.push_back(userJson);
    }
    return jsonList;
}

void MecOrchestrator::handleUALCMPMessage(cMessage* msg)
{
    UALCMPMessage* lcmMsg = check_and_cast<UALCMPMessage*>(msg);

    /* Handling CREATE_CONTEXT_APP */
    if(!strcmp(lcmMsg->getType(), CREATE_CONTEXT_APP))
        startMECApp(lcmMsg);

    /* Handling DELETE_CONTEXT_APP */
    else if(!strcmp(lcmMsg->getType(), DELETE_CONTEXT_APP))
        stopMECApp(lcmMsg);

    /* Handling confirmation of MEH change*/
    else if(!strcmp(lcmMsg->getType(), ACK_UPDATE_MEH_IP))
        handleMehChangeAck(lcmMsg);
    
    else
        throw cRuntimeError("MecOrchestrator::handleUALCMPMessage - Message type %s not recognized", lcmMsg->getType());

}

void MecOrchestrator::handleMehChangeAck(UALCMPMessage* msg)
{
    EV << "MecOrchestrator::handleMehChangeAck - processing..." << endl;
    UpdateMEHAckMessage* mehChangeAck = check_and_cast<UpdateMEHAckMessage*>(msg);

    unsigned int request = mehChangeAck->getRequestNumber();
    bool result = mehChangeAck->getSucess();

    EV << "MecOrchestrator::handleMehChangeAck - MEH change request ["<< request << "] result: " << result << endl;

    if(result)
    {
        EV << "MecOrchestrator::handleMehChangeAck - MEH change request "<< request << " successfully processed" << endl;
    
        // now we need to check the requestNumber is in the standByList, if so we need to stop the app and remove the entry from the standByList
        if(standByList.find(request) != standByList.end())
        {
            standByElement element = standByList[request];
            int mecUeAppId = element.mecUeAppID;

            MecPlatformManager* mecpm = check_and_cast<MecPlatformManager*>(element.mecpm);

            DeleteAppMessage* deleteAppMsg = new DeleteAppMessage();
            deleteAppMsg->setUeAppID(mecUeAppId);

            bool isTerminated;
            isTerminated =  mecpm->terminateMEApp(deleteAppMsg);
            
            if(isTerminated)
            {
                EV << "MecOrchestrator::handleMehChangeAck - mec Application with mecUeAppId ["<< element.mecUeAppID << "] removed"<< endl;
                //std::cout << "MigrateOnChange" << endl;
                standByList.erase(mehChangeAck->getRequestNumber());
                EV << "MecOrchestrator::handleMehChangeAck - standByList entry removed" << endl;
            }
            else
            {
                EV << "MecOrchestrator::handleMehChangeAck - something went wrong during MEC app termination"<< endl;
            
            }
        }
        else
        {
            EV << "MecOrchestrator::handleMehChangeAck - standByList entry with request number "<< request << " not found" << endl;
        }
    }
    else
    {
        EV << "MecOrchestrator::handleMehChangeAck - MEH change request ["<< mehChangeAck->getRequestNumber() << "] failed" << endl;
    }
}

void MecOrchestrator::startMECApp(UALCMPMessage* msg)
{
    CreateContextAppMessage* contAppMsg = check_and_cast<CreateContextAppMessage*>(msg);

    EV << "MecOrchestrator::createMeApp - processing... request id: " << contAppMsg->getRequestId() << endl;

    //retrieve UE App ID
    int ueAppID = atoi(contAppMsg->getDevAppId());

    /*
     * The Mec orchestrator has to decide where to deploy the MEC application.
     * - It checks if the MEC app has been already deployed
     * - It selects the most suitable MEC host     *
     */

    for(const auto& contextApp : meAppMap)
    {
        /*
         * TODO
         * set the check to provide multi UE to one mec application scenario.
         * For now the scenario is one to one, since the device application ID is used
         */
        if(contextApp.second.mecUeAppID == ueAppID && contextApp.second.appDId.compare(contAppMsg->getAppDId()) == 0)
        {
            //        meAppMap[ueAppID].lastAckStartSeqNum = pkt->getSno();
            //Sending ACK to the UEApp to confirm the instantiation in case of previous ack lost!
            //        ackMEAppPacket(ueAppID, ACK_START_MEAPP);
            //testing
            EV << "MecOrchestrator::startMECApp - \tWARNING: required MEC App instance ALREADY STARTED on MEC host: " << contextApp.second.mecHost->getName() << endl;
            EV << "MecOrchestrator::startMECApp  - sending ackMEAppPacket with "<< ACK_CREATE_CONTEXT_APP << endl;
            sendCreateAppContextAck(true, contAppMsg->getRequestId(), contextApp.first);
            return;
        }
    }


    std::string appDid;
    double processingTime = 0.0;

    if(contAppMsg->getOnboarded() == false)
    {
        // onboard app descriptor
        EV << "MecOrchestrator::startMECApp - onboarding appDescriptor from: " << contAppMsg->getAppPackagePath() << endl;
        const ApplicationDescriptor& appDesc = onboardApplicationPackage(contAppMsg->getAppPackagePath());
        appDid = appDesc.getAppDId();
        processingTime += onboardingTime;
    }
    else
    {
       appDid = contAppMsg->getAppDId();
    }

    auto it = mecApplicationDescriptors_.find(appDid);
    if(it == mecApplicationDescriptors_.end())
    {
        EV << "MecOrchestrator::startMECApp - Application package with AppDId["<< contAppMsg->getAppDId() << "] not onboarded." << endl;
        sendCreateAppContextAck(false, contAppMsg->getRequestId());
//        throw cRuntimeError("MecOrchestrator::startMECApp - Application package with AppDId[%s] not onboarded", contAppMsg->getAppDId());
    }

    const ApplicationDescriptor& desc = it->second;

    inet::L3Address ueAddress = inet::L3AddressResolver().resolve(contAppMsg->getUeIpAddress());

    cModule* bestHost = mecHostSelectionPolicy_->findBestMecHost(desc, ueAddress);


    if(bestHost != nullptr)
    {
        CreateAppMessage * createAppMsg = new CreateAppMessage();

        createAppMsg->setUeAppID(atoi(contAppMsg->getDevAppId()));
        createAppMsg->setMEModuleName(desc.getAppName().c_str());
        createAppMsg->setMEModuleType(desc.getAppProvider().c_str());

        createAppMsg->setRequiredCpu(desc.getVirtualResources().cpu);
        createAppMsg->setRequiredRam(desc.getVirtualResources().ram);
        createAppMsg->setRequiredDisk(desc.getVirtualResources().disk);

        // This field is useful for mec services no etsi mec compliant (e.g. omnet++ like)
        // In such case, the vim has to connect the gates between the mec application and the service

        // insert OMNeT like services, only one is supported, for now
        if(!desc.getOmnetppServiceRequired().empty())
            createAppMsg->setRequiredService(desc.getOmnetppServiceRequired().c_str());
        else
            createAppMsg->setRequiredService("NULL");

        createAppMsg->setContextId(contextIdCounter);

     // add the new mec app in the map structure
        mecAppMapEntry newMecApp;
        newMecApp.appDId = appDid;
        newMecApp.mecUeAppID =  ueAppID;
        newMecApp.mecHost = bestHost;
        newMecApp.ueAddress = inet::L3AddressResolver().resolve(contAppMsg->getUeIpAddress());
        newMecApp.vim = bestHost->getSubmodule("vim");
        newMecApp.mecpm = bestHost->getSubmodule("mecPlatformManager");

        newMecApp.mecAppName = desc.getAppName().c_str();
//        newMecApp.lastAckStartSeqNum = pkt->getSno();


         MecPlatformManager* mecpm = check_and_cast<MecPlatformManager*>(newMecApp.mecpm);

         /*
          * If the application descriptor refers to a simulated mec app, the system eventually instances the mec app object.
          * If the application descriptor refers to a mec application running outside the simulator, i.e emulation mode,
          * the system allocates the resources, without instantiate any module.
          * The application descriptor contains the address and port information to communicate with the mec application
          */

         MecAppInstanceInfo* appInfo = nullptr;

         if(desc.isMecAppEmulated())
         {
             EV << "MecOrchestrator::startMECApp - MEC app is emulated" << endl;
             bool result = mecpm->instantiateEmulatedMEApp(createAppMsg);
             appInfo = new MecAppInstanceInfo();
             appInfo->status = result;
             appInfo->endPoint.addr = inet::L3Address(desc.getExternalAddress().c_str());
             appInfo->endPoint.port = desc.getExternalPort();
             appInfo->instanceId = "emulated_" + desc.getAppName();
             newMecApp.isEmulated = true;

             // register the address of the MEC app to the Binder, so as the GTP knows the endpoint (UPF_MEC) where to forward packets to
             inet::L3Address gtpAddress = inet::L3AddressResolver().resolve(newMecApp.mecHost->getSubmodule("upf_mec")->getFullPath().c_str());
             binder_->registerMecHostUpfAddress(appInfo->endPoint.addr, gtpAddress);
         }
         else
         {
             appInfo = mecpm->instantiateMEApp(createAppMsg);
             newMecApp.isEmulated = false;
         }

         if(!appInfo->status)
         {
             EV << "MecOrchestrator::startMECApp - something went wrong during MEC app instantiation"<< endl;
             MECOrchestratorMessage *msg = new MECOrchestratorMessage("MECOrchestratorMessage");
             msg->setType(CREATE_CONTEXT_APP);
             msg->setRequestId(contAppMsg->getRequestId());
             msg->setSuccess(false);
             processingTime += instantiationTime;
             scheduleAt(simTime() + processingTime, msg);
             return;
         }

         EV << "MecOrchestrator::startMECApp - new MEC application with name: " << appInfo->instanceId << " instantiated on MEC host []"<< newMecApp.mecHost << " at "<< appInfo->endPoint.addr.str() << ":" << appInfo->endPoint.port << endl;

         newMecApp.mecAppAddress = appInfo->endPoint.addr;
         newMecApp.mecAppPort = appInfo->endPoint.port;
         newMecApp.mecAppIsntanceId = appInfo->instanceId;
         newMecApp.contextId = contextIdCounter;
         meAppMap[contextIdCounter] = newMecApp;

         MECOrchestratorMessage *msg = new MECOrchestratorMessage("MECOrchestratorMessage");
         msg->setContextId(contextIdCounter);
         msg->setType(CREATE_CONTEXT_APP);
         msg->setRequestId(contAppMsg->getRequestId());
         msg->setSuccess(true);

         contextIdCounter++;

         processingTime += instantiationTime;
         scheduleAt(simTime() + processingTime, msg);

         delete appInfo;
    }
    else
    {
        //throw cRuntimeError("MecOrchestrator::startMECApp - A suitable MEC host has not been selected");
        EV << "MecOrchestrator::startMECApp - A suitable MEC host has not been selected" << endl;
        MECOrchestratorMessage *msg = new MECOrchestratorMessage("MECOrchestratorMessage");
        msg->setType(CREATE_CONTEXT_APP);
        msg->setRequestId(contAppMsg->getRequestId());
        msg->setSuccess(false);
        processingTime += instantiationTime/2;
        scheduleAt(simTime() + processingTime, msg);
    }

}

void MecOrchestrator::stopMECApp(unsigned int ref)
{
    EV << "MeccOrchestrator::stopMECApp - processing... ref: "<< ref << " to stop the correspondent app on the meh" << endl;
}

void MecOrchestrator::stopMECApp(UALCMPMessage* msg){
    EV << "MecOrchestrator::stopMECApp - processing..." << endl;

    DeleteContextAppMessage* contAppMsg = check_and_cast<DeleteContextAppMessage*>(msg);

    int contextId = contAppMsg->getContextId();
    EV << "MecOrchestrator::stopMECApp - processing contextId: "<< contextId << endl;
    // checking if ueAppIdToMeAppMapKey entry map does exist
    if(meAppMap.empty() || (meAppMap.find(contextId) == meAppMap.end()))
    {
        // maybe it has already been deleted
        EV << "MecOrchestrator::stopMECApp - \tWARNING Mec Application ["<< meAppMap[contextId].mecUeAppID <<"] not found!" << endl;
        sendDeleteAppContextAck(false, contAppMsg->getRequestId(), contextId);
//        throw cRuntimeError("MecOrchestrator::stopMECApp - \tERROR ueAppIdToMeAppMapKey entry not found!");
        return;
    }

    // call the methods of resource manager and virtualization infrastructure of the selected mec host to deallocate the resources

     MecPlatformManager* mecpm = check_and_cast<MecPlatformManager*>(meAppMap[contextId].mecpm);
//     VirtualisationInfrastructureManager* vim = check_and_cast<VirtualisationInfrastructureManager*>(meAppMap[contextId].vim);

     DeleteAppMessage* deleteAppMsg = new DeleteAppMessage();
     deleteAppMsg->setUeAppID(meAppMap[contextId].mecUeAppID);

     bool isTerminated;
     if(meAppMap[contextId].isEmulated)
     {
         isTerminated =  mecpm->terminateEmulatedMEApp(deleteAppMsg);
         std::cout << "terminateEmulatedMEApp with result: " << isTerminated << std::endl;
     }
     else
     {
         isTerminated =  mecpm->terminateMEApp(deleteAppMsg);
     }

     MECOrchestratorMessage *mecoMsg = new MECOrchestratorMessage("MECOrchestratorMessage");
     mecoMsg->setType(DELETE_CONTEXT_APP);
     mecoMsg->setRequestId(contAppMsg->getRequestId());
     mecoMsg->setContextId(contAppMsg->getContextId());
     if(isTerminated)
     {
         EV << "MecOrchestrator::stopMECApp - mec Application ["<< meAppMap[contextId].mecUeAppID << "] removed"<< endl;
         meAppMap.erase(contextId);
         mecoMsg->setSuccess(true);
     }
     else
     {
         EV << "MecOrchestrator::stopMECApp - mec Application ["<< meAppMap[contextId].mecUeAppID << "] not removed"<< endl;
         mecoMsg->setSuccess(false);
     }

    double processingTime = terminationTime;
    scheduleAt(simTime() + processingTime, mecoMsg);

}

void MecOrchestrator::sendMehChangeRequest(std::string ueAddress, std::string newMehId, int newPort, unsigned int requestNumber)
{
    EV << "MecOrchestrator::sendMehChangeRequest - sending MEH change request to UALCMP" << endl;
    UpdateMEHMessage *mehChangeRequest = new UpdateMEHMessage();
    mehChangeRequest->setType(UPDATE_MEH_IP);
    mehChangeRequest->setUeIpAddress(ueAddress.c_str());
    mehChangeRequest->setNewMehIpAddress(newMehId.c_str());
    mehChangeRequest->setNewMehPort(newPort);
    mehChangeRequest->setRequestNumber(requestNumber);

    //send(mehChangeRequest, "toUALCMP");
    sendDelayed(mehChangeRequest, migrationTime, "toUALCMP");
}

void MecOrchestrator::sendDeleteAppContextAck(bool result, unsigned int requestSno, int contextId)
{
    EV << "MecOrchestrator::sendDeleteAppContextAck - result: "<<result << " reqSno: " << requestSno << " contextId: " << contextId << endl;
    DeleteContextAppAckMessage * ack = new DeleteContextAppAckMessage();
    ack->setType(ACK_DELETE_CONTEXT_APP);
    ack->setRequestId(requestSno);
    ack->setSuccess(result);

    send(ack, "toUALCMP");
}

void MecOrchestrator::sendCreateAppContextAck(bool result, unsigned int requestSno, int contextId)
{
    EV << "MecOrchestrator::sendCreateAppContextAck - result: "<< result << " reqSno: " << requestSno << " contextId: " << contextId << endl;
    CreateContextAppAckMessage *ack = new CreateContextAppAckMessage();
    ack->setType(ACK_CREATE_CONTEXT_APP);

    if(result)
    {
        if(meAppMap.empty() || meAppMap.find(contextId) == meAppMap.end())
        {
            EV << "MecOrchestrator::ackMEAppPacket - ERROR meApp["<< contextId << "] does not exist!" << endl;
//            throw cRuntimeError("MecOrchestrator::ackMEAppPacket - ERROR meApp[%d] does not exist!", contextId);
            return;
        }

        mecAppMapEntry mecAppStatus = meAppMap[contextId];

        ack->setSuccess(true);
        ack->setContextId(contextId);
        ack->setAppInstanceId(mecAppStatus.mecAppIsntanceId.c_str());
        ack->setRequestId(requestSno);
        std::stringstream uri;

        uri << mecAppStatus.mecAppAddress.str()<<":"<<mecAppStatus.mecAppPort;

        ack->setAppInstanceUri(uri.str().c_str());
    }
    else
    {
        ack->setRequestId(requestSno);
        ack->setSuccess(false);
    }
    send(ack, "toUALCMP");
}


cModule* MecOrchestrator::findBestMecHost(const ApplicationDescriptor& appDesc)
{

    EV << "MecOrchestrator::findBestMecHost - finding best MecHost..." << endl;
    cModule* bestHost = nullptr;

    for(auto mecHost : mecHosts)
    {
        VirtualisationInfrastructureManager *vim = check_and_cast<VirtualisationInfrastructureManager*> (mecHost->getSubmodule("vim"));
        ResourceDescriptor resources = appDesc.getVirtualResources();
        bool res = vim->isAllocable(resources.ram, resources.disk, resources.cpu);
        if(!res)
        {
            EV << "MecOrchestrator::findBestMecHost - MEC host []"<< mecHost << " has not got enough resources. Searching again..." << endl;
            continue;
        }

        // Temporally select this mec host as the best
        EV << "MecOrchestrator::findBestMecHost - MEC host []"<< mecHost << " temporally chosen as bet MEC host, checking for the required MEC services.." << endl;
        bestHost = mecHost;

        MecPlatformManager *mecpm = check_and_cast<MecPlatformManager*> (mecHost->getSubmodule("mecPlatformManager"));
        auto mecServices = mecpm ->getAvailableMecServices();
        std::string serviceName;

        /* I assume the app requires only one mec service */
        if(appDesc.getAppServicesRequired().size() > 0)
        {
            serviceName =  appDesc.getAppServicesRequired()[0];
        }
        else
        {
            break;
        }
        auto it = mecServices->begin();
        for(; it != mecServices->end() ; ++it)
        {
            if(serviceName.compare(it->getName()) == 0)
            {
               bestHost = mecHost;
               break;
            }
        }
    }
    if(bestHost != nullptr)
        EV << "MecOrchestrator::findBestMecHost - best MEC host: " << bestHost << endl;
    else
        EV << "MecOrchestrator::findBestMecHost - no MEC host found"<< endl;

    return bestHost;
}

void MecOrchestrator::getConnectedMecHosts()
{
    //getting the list of mec hosts associated to this mec system from parameter
    if(this->hasPar("mecHostList") && strcmp(par("mecHostList").stringValue(), "")){
        std::string mecHostList = par("mecHostList").stdstringValue();
        EV <<"MecOrchestrator::getConnectedMecHosts - mecHostList: "<< par("mecHostList").stringValue() << endl;
        char* token = strtok ( (char*)mecHostList.c_str(), ", ");            // split by commas

        while (token != NULL)
        {
            EV <<"MecOrchestrator::getConnectedMecHosts - mec host (from par): "<< token << endl;
            cModule *mecHostModule = getSimulation()->getModuleByPath(token);
            mecHosts.push_back(mecHostModule);
            token = strtok (NULL, ", ");
        }
    }
    else{
//        throw cRuntimeError ("MecOrchestrator::getConnectedMecHosts - No mecHostList found");
        EV << "MecOrchestrator::getConnectedMecHosts - No mecHostList found" << endl;
    }

}

const ApplicationDescriptor& MecOrchestrator::onboardApplicationPackage(const char* fileName)
{
    EV <<"MecOrchestrator::onBoardApplicationPackages - onboarding application package (from request): "<< fileName << endl;
    ApplicationDescriptor appDesc(fileName);
    if(mecApplicationDescriptors_.find(appDesc.getAppDId()) != mecApplicationDescriptors_.end())
    {
        EV << "MecOrchestrator::onboardApplicationPackages() - Application descriptor with appName ["<< fileName << "] is already present.\n" << endl;
//        throw cRuntimeError("MecOrchestrator::onboardApplicationPackages() - Application descriptor with appName [%s] is already present.\n"
//                            "Duplicate appDId or application package already onboarded?", fileName);
    }
    else
    {
        mecApplicationDescriptors_[appDesc.getAppDId()] = appDesc; // add to the mecApplicationDescriptors_
    }

    return mecApplicationDescriptors_[appDesc.getAppDId()];
}

void MecOrchestrator::registerMecService(ServiceDescriptor& serviceDescriptor) const
{
    EV << "MecOrchestrator::registerMecService - Registering MEC service [" << serviceDescriptor.name << "]" << endl;
    for(auto mecHost : mecHosts)
    {
        cModule* module = mecHost->getSubmodule("mecPlatform")->getSubmodule("serviceRegistry");
        if(module != nullptr)
        {
            EV << "MecOrchestrator::registerMecService - Registering MEC service ["<<serviceDescriptor.name << "] in MEC host [" << mecHost->getName()<<"]" << endl;
            ServiceRegistry* serviceRegistry = check_and_cast<ServiceRegistry*>(module);
            serviceRegistry->registerMecService(serviceDescriptor);
        }
    }
}

void MecOrchestrator::onboardApplicationPackages()
{
    //getting the list of mec hosts associated to this mec system from parameter
    if(this->hasPar("mecApplicationPackageList") && strcmp(par("mecApplicationPackageList").stringValue(), "")){

        char* token = strtok ( (char*) par("mecApplicationPackageList").stringValue(), ", ");            // split by commas

        while (token != NULL)
        {
            int len = strlen(token);
            char buf[len+strlen(".json")+strlen("ApplicationDescriptors/")+1];
            strcpy(buf,"ApplicationDescriptors/");
            strcat(buf,token);
            strcat(buf,".json");
            onboardApplicationPackage(buf);
            token = strtok (NULL, ", ");
        }
    }
    else{
        EV << "MecOrchestrator::onboardApplicationPackages - No mecApplicationPackageList found" << endl;
    }

}

// Callback function to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string MecOrchestrator::postRequestPrediction(const std::string &url, const nlohmann::json &jsonObject) 
{
    CURL *curl;
    CURLcode res;
    std::string response;

    curl = curl_easy_init();
    if (!curl) {
        EV << "Failed to initialize cURL!" << endl;
        return "";
    }

    // Convert JSON object to string
    std::string jsonString = jsonObject.dump();

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Set HTTP headers
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Set POST data
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());

    // Set callback function to capture response data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Perform the request, res will get the return code
    res = curl_easy_perform(curl);

    // Check for errors
    if (res != CURLE_OK) {
        EV << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        EV << "Response code: " << response_code << endl;
        EV << "Response body: " << response << endl;
    }

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    return response;
}

void MecOrchestrator::removeAppTime(std::string ueAddress, std::string oldMEHId)
{
    std::string ueIp = ueAddress.substr(4);
    inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

    // remove user from the userMEHMap
    userMEHMap.erase(ueAddress);

    // requestId will be zero so the UALCMP will not try to send a response to the UE when receives the ack from the MEO
    int requestId = 0; // I've changed the starting value of the request counter to 1

    // find the contextId that has the same ueAddress
    int contextId = -1;
    for (auto &it : meAppMap)
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
    stopMECApp(msg);
}

void MecOrchestrator::migrateAppTime(std::string ueAddress, std::string newMEHId, std::string oldMEHId)
{
    // this is a replication of what happens on the MigrationOnChange class. TODO: That class will use this method in the future. 
    std::string ueIp = ueAddress.substr(4);
    inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

    int contextId = -1;
    for (auto &it : meAppMap)
    {
        if (it.second.ueAddress == ueL3Address)
        {
            EV << "MecOrchestrator::MigrateAppTime - contextId found for ueAddress " << ueAddress << " is " << it.first << endl;
            contextId = it.first;
            break;
        }
    }

    if (contextId == -1)
    {
        EV << "MecOrchestrator::MigrateAppTime - contextId not found for ueAddress " << ueAddress << endl;
        return;
    }
    else
    {
        auto meAppMapEntry = meAppMap.find(contextId);
        const ApplicationDescriptor& desc = mecApplicationDescriptors_.at(meAppMapEntry->second.appDId);

        standByElement standBy;
        standBy.mecUeAppID = meAppMapEntry->second.mecUeAppID;
        standBy.mecpm = meAppMapEntry->second.mecpm; 

        cModule *newMEH = nullptr;
        for (auto &it : mecHosts)
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
        meAppMap[meAppMapEntry->first] = meAppMapEntry->second;

        standBy.request = requestCounter;
        requestCounter++; 

        // 2. Inform the UE about the new MEH (through the UALCMP)
        sendMehChangeRequest(ueIp, appInfo->endPoint.addr.str(), appInfo->endPoint.port, standBy.request);

        // 3. Add the UE to a standby list until the ACK from the UE is received
        standByList[standBy.request] = standBy; 

        delete appInfo;
    }

}


const ApplicationDescriptor* MecOrchestrator::getApplicationDescriptorByAppName(std::string& appName) const
{
    for(const auto& appDesc : mecApplicationDescriptors_)
    {
        if(appDesc.second.getAppName().compare(appName) == 0)
            return &(appDesc.second);

    }

    return nullptr;
}

} //namespace

