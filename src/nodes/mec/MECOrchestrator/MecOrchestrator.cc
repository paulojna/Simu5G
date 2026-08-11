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
#include <omnetpp/cmodule.h>

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
#include "nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/MigrateOnPrediction.h"

#include "apps/mec/RavensApps/RavensControlPacket_m.h"

#include "nodes/mec/MECOrchestrator/ApplicationDescriptor/ApplicationDescriptor.h"

// emulation debug
#include <iostream>
#include <curl/curl.h>

namespace simu5g
{

// Helper function to replace check_and_cast for IDE compatibility (it is not recognized by OMNET++ IDE)
template<typename T, typename U>
T* safe_check_and_cast(U* ptr) {
    T* result = dynamic_cast<T*>(ptr);
    if (result == nullptr) {
        const char* className = ptr ? (ptr->getClassName() ? ptr->getClassName() : "unknown") : nullptr;
        throw cRuntimeError("check_and_cast failed: cannot cast %s to %s", 
                          className, opp_typename(typeid(T)));
    }
    return result;
}

    Define_Module(MecOrchestrator);

    MecOrchestrator::MecOrchestrator()
    {
        mecHostSelectionPolicy_ = nullptr;
        userMEHMap.clear();
        reactionOnUpdate_ = nullptr;
        // NEW
        mecAppRegistry_ = nullptr;
        mecAppLifecycleManager_ = nullptr;
        mecAppMigrationManager_ = nullptr;
    }

    MecOrchestrator::~MecOrchestrator()
    {
        delete mecHostSelectionPolicy_;
        delete reactionOnUpdate_;
    }

    void MecOrchestrator::initialize(int stage)
    {
        cSimpleModule::initialize(stage);
        // avoid multiple initializations
        if (stage != inet::INITSTAGE_LOCAL)
            return;
        EV << "MecOrchestrator::initialize - stage " << stage << endl;

        binder_ = getBinder();

        if (!strcmp(par("selectionPolicy"), "MecServiceBased"))
            mecHostSelectionPolicy_ = new MecServiceSelectionBased(this);
        else if (!strcmp(par("selectionPolicy"), "AvailableResourcesBased"))
            mecHostSelectionPolicy_ = new AvailableResourcesSelectionBased(this);
        else if (!strcmp(par("selectionPolicy"), "MecHostBased"))
            mecHostSelectionPolicy_ = new MecHostSelectionBased(this, par("mecHostIndex"));
        else if (!strcmp(par("selectionPolicy"), "LocationBased"))
            mecHostSelectionPolicy_ = new LocationSelectionBased(this);
        else
            throw cRuntimeError("MecOrchestrator::initialize - Selection policy %s not present!", par("selectionPolicy").stringValue());

        onboardingTime = par("onboardingTime").doubleValue();
        instantiationTime = par("instantiationTime").doubleValue();
        terminationTime = par("terminationTime").doubleValue();
        migrationTime_ = par("migrationTime").doubleValue();
        migrationTimeout_ = par("migrationTimeout").doubleValue();

        getConnectedMecHosts();
        //onboardApplicationPackages();

        // NEW
        mecAppRegistry_ = std::make_unique<MecAppRegistry>();
        mecAppLifecycleManager_ = std::make_unique<MecAppLifecycleManager>(
            mecAppRegistry_.get(), mecHostSelectionPolicy_
        );
        mecAppLifecycleManager_->initialize(onboardingTime, instantiationTime, terminationTime);

        if (!strcmp(par("reactionStrategy"), "RemoveOnExit"))
        {
            reactionOnUpdate_ = new RemoveOnExit(static_cast<IOrchestratorApi *>(this));
        }
        else if (!strcmp(par("reactionStrategy"), "MigrateOnChange"))
        {
            mecAppMigrationManager_ = std::make_unique<MecAppMigrationManager>(
              mecAppRegistry_.get(),
              mecAppLifecycleManager_.get(),
              &mecHosts,
              &mecHostIndex_,
              this
            );
            mecAppMigrationManager_->initialize(migrationTime_, migrationTimeout_);

            reactionOnUpdate_ = new MigrateOnChange(static_cast<IOrchestratorApi *>(this));
        }
        else if (!strcmp(par("reactionStrategy"), "MigrateOnPrediction"))
        {
            mecAppMigrationManager_ = std::make_unique<MecAppMigrationManager>(
              mecAppRegistry_.get(),
              mecAppLifecycleManager_.get(),
              &mecHosts,
              &mecHostIndex_,
              this
            );
            mecAppMigrationManager_->initialize(migrationTime_, migrationTimeout_);

            reactionOnUpdate_ = new MigrateOnPrediction(
                static_cast<IOrchestratorApi *>(this), this, migrationTime_);
        }
        else
            throw cRuntimeError("MecOrchestrator::initialize - Reaction strategy %s not present!", par("reactionStrategy").stringValue());

        if (!reactionOnUpdate_)
            throw cRuntimeError("MecOrchestrator::initialize - reactionOnUpdate_ is null");

        mecAppLifecycleManager_->onboardApplicationPackages(par("mecApplicationPackageList").stringValue());

    }

    void MecOrchestrator::handleMessage(cMessage *msg)
    {
        if (msg->isSelfMessage())
        {
            if (strcmp(msg->getName(), "MECOrchestratorMessage") == 0)
            {
                EV << "MecOrchestrator::handleMessage - " << msg->getName() << endl;
                MECOrchestratorMessage *meoMsg = safe_check_and_cast<MECOrchestratorMessage>(msg);
                if (strcmp(meoMsg->getType(), CREATE_CONTEXT_APP) == 0)
                {
                    if (meoMsg->getSuccess())
                        sendCreateAppContextAck(true, meoMsg->getRequestId(), meoMsg->getContextId());
                    else
                        sendCreateAppContextAck(false, meoMsg->getRequestId());
                }
                else if (strcmp(meoMsg->getType(), DELETE_CONTEXT_APP) == 0)
                    sendDeleteAppContextAck(meoMsg->getSuccess(), meoMsg->getRequestId(), meoMsg->getContextId());
            }
            else if (strcmp(msg->getName(), "UpdateFromRAVENS") == 0)
            {
                EV << "I'm going to do something here" << endl;
            }
            else if (strcmp(msg->getName(), "MigrationTimeout") == 0)
            {
                MigrateTimeoutMessage* timeoutMsg = check_and_cast<MigrateTimeoutMessage*>(msg);
                mecAppMigrationManager_->handleMigrationTimeout(timeoutMsg->getRequestNumber());
            }
            else if (strcmp(msg->getName(), "ScheduledMigration") == 0)
            {
                reactionOnUpdate_->handleScheduledEvent(msg);
            }
        }
        // handle message from the LCM proxy
        else if (msg->arrivedOn("fromUALCMP"))
        {
            EV << "MecOrchestrator::handleMessage - " << msg->getName() << endl;
            handleUALCMPMessage(msg);
        }
        else if (msg->arrivedOn("fromRavensController"))
        {
            EV << "MecOrchestrator::handleMessage fromRavensController - " << msg->getName() << endl;

            inet::Packet *packet = safe_check_and_cast<inet::Packet>(msg);
            auto received_packet = packet->peekAtFront<RavensControlPacket>();

            switch (received_packet->getType())
            {
            case USER_EVENT: {
                // One confirmed change, and it arrived the moment RAVENS
                // concluded it. Which of fromMEHId / toMEHId are set follows from
                // the event type; nothing here infers the event from them.
                // The chunk is held in a named variable rather than chained,
                // so the reference below cannot outlive it.
                auto eventMessage = packet->peekAtFront<UserEventMessage>();
                const UserEvent& event = eventMessage->getEvent();
                EV << "MecOrchestrator::handleMessage - " << userEventTypeName(event.eventType)
                   << " " << event.ueAddress << " '" << event.fromMEHId << "' -> '"
                   << event.toMEHId << "'" << endl;

                // An exit leaves toMEHId empty, which is what should be recorded:
                // the user is no longer anywhere.
                userMEHMap[event.ueAddress] = {event.ueAddress, event.toMEHId};
                reactionOnUpdate_->reactOnUpdate(event);
                break;
            }

            case PREDICTION_REPORT: {
                auto report = packet->peekAtFront<PredictionReportMessage>();
                const std::vector<MigrationPrediction>& predictions = report->getPredictions();
                EV << "MecOrchestrator::handleMessage - " << predictions.size()
                   << " predictions received" << endl;
                reactionOnUpdate_->reactOnUpdate(predictions);
                break;
            }

            case TELEMETRY_REPORT: {
                // Forwarded on every run that collects telemetry, and consumed
                // only by a strategy that asked for it — the default
                // implementation of reactOnTelemetry ignores it. Being sent
                // regardless is what makes the learning mode a configuration
                // change rather than a different code path.
                auto report = packet->peekAtFront<TelemetryReportMessage>();
                reactionOnUpdate_->reactOnTelemetry(report->getWindowStart(),
                                                    report->getWindowEnd(),
                                                    report->getReportingMEHIds(),
                                                    report->getUserSamples(),
                                                    report->getCellSamples());
                break;
            }

            default:
                EV << "MecOrchestrator::handleMessage - unknown RAVENS stream type "
                   << received_packet->getType() << ", ignoring" << endl;
                break;
            }
        }

        delete msg;
        return;
    }

    void MecOrchestrator::handleUALCMPMessage(cMessage *msg)
    {
        UALCMPMessage *lcmMsg = safe_check_and_cast<UALCMPMessage>(msg);

        /* Handling CREATE_CONTEXT_APP */
        if (!strcmp(lcmMsg->getType(), CREATE_CONTEXT_APP))
        {
            LifecycleResult result = mecAppLifecycleManager_->startApplication(lcmMsg);
            if(result.success) {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - CREATE_CONTEXT_APP success, contextId: " << result.contextId << std::endl;
                sendCreateAppContextAck(true, lcmMsg->getRequestId(), result.contextId);
            }
            else {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - CREATE_CONTEXT_APP failed, contextId: " << result.contextId << std::endl;
                sendCreateAppContextAck(false, lcmMsg->getRequestId());
            }
        }
        /* Handling DELETE_CONTEXT_APP */
        else if (!strcmp(lcmMsg->getType(), DELETE_CONTEXT_APP))
        {
            LifecycleResult result = mecAppLifecycleManager_->stopApplication(lcmMsg);
            if(result.success) {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - DELETE_CONTEXT_APP success, contextId: " << result.contextId << std::endl;
                sendDeleteAppContextAck(true, lcmMsg->getRequestId(), result.contextId);
            }
            else {
                //std::cout << "MecOrchestrator::handleUALCMPMessage - DELETE_CONTEXT_APP failed, contextId: " << result.contextId << std::endl;
                sendDeleteAppContextAck(false, lcmMsg->getRequestId());
            }
        }
        /* Handling confirmation of MEH change*/
        else if (!strcmp(lcmMsg->getType(), ACK_UPDATE_MEH_IP))
        {
            //std::cout << "ACK_UPDATE_MEH_IP RECEIVED!!" << endl;
            if (!mecAppMigrationManager_) 
            {
                EV << "MecOrchestrator::handleUALCMPMessage - Migration manager not initialized" << endl;
                //std::cout << "BIG PROBLEMS!!" << endl;
                return;
            }

            MigrationResult result = mecAppMigrationManager_->completeMigration(lcmMsg);
            if (!result.success) 
            {
                EV << "MecOrchestrator::handleUALCMPMessage - Migration completion failed: " << result.errorMessage << endl;
            } 
            else 
            {
                EV << "MecOrchestrator::handleUALCMPMessage - Migration completed successfully" << endl;
            }
        }
        else
            throw cRuntimeError("MecOrchestrator::handleUALCMPMessage - Message type %s not recognized", lcmMsg->getType());
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

        // send(mehChangeRequest, "toUALCMP");
        sendDelayed(mehChangeRequest, migrationTime_, "toUALCMP");
    }

    void MecOrchestrator::sendDeleteAppContextAck(bool result, unsigned int requestSno, int contextId)
    {
        EV << "MecOrchestrator::sendDeleteAppContextAck - result: " << result << " reqSno: " << requestSno << " contextId: " << contextId << endl;
        DeleteContextAppAckMessage *ack = new DeleteContextAppAckMessage();
        ack->setType(ACK_DELETE_CONTEXT_APP);
        ack->setRequestId(requestSno);
        ack->setSuccess(result);

        send(ack, "toUALCMP");
    }

    void MecOrchestrator::sendCreateAppContextAck(bool result, unsigned int requestSno, int contextId)
    {
        EV << "MecOrchestrator::sendCreateAppContextAck - result: " << result << " reqSno: " << requestSno << " contextId: " << contextId << endl;
        CreateContextAppAckMessage *ack = new CreateContextAppAckMessage();
        ack->setType(ACK_CREATE_CONTEXT_APP);

        if (result)
        {
            // NEW 
            auto result = mecAppRegistry_->findAppByContextId(contextId);
            if(!result.found)
            {
                EV << "MecOrchestrator::sendCreateAppContextAck - ERROR meApp[" << contextId << "] does not exist!" << endl;
                return;
            }

            const MecAppRegistry::AppEntry& mecAppStatus = *result.appEntry;

            ack->setSuccess(true);
            ack->setContextId(contextId);
            ack->setRequestId(requestSno);

            //NEW
            ack->setAppInstanceId(mecAppStatus.mecAppInstanceId.c_str());
            ack->setAppInstanceUri(mecAppStatus.getEndpointString().c_str());
        }
        else
        {
            ack->setRequestId(requestSno);
            ack->setSuccess(false);
        }
        send(ack, "toUALCMP");
    }

    cModule *MecOrchestrator::findBestMecHost(const ApplicationDescriptor &appDesc)
    {

        EV << "MecOrchestrator::findBestMecHost - finding best MecHost..." << endl;
        cModule *bestHost = nullptr;

        for (auto mecHost : mecHosts)
        {
            VirtualisationInfrastructureManager *vim = dynamic_cast<VirtualisationInfrastructureManager *>(mecHost->getSubmodule("vim"));
            if (vim == nullptr) {
                throw cRuntimeError("check_and_cast failed: cannot cast submodule 'vim' to VirtualisationInfrastructureManager");
            }
            ResourceDescriptor resources = appDesc.getVirtualResources();
            bool res = vim->isAllocable(resources.ram, resources.disk, resources.cpu);
            if (!res)
            {
                EV << "MecOrchestrator::findBestMecHost - MEC host []" << mecHost << " has not got enough resources. Searching again..." << endl;
                continue;
            }

            // Temporally select this mec host as the best
            EV << "MecOrchestrator::findBestMecHost - MEC host []" << mecHost << " temporally chosen as bet MEC host, checking for the required MEC services.." << endl;
            bestHost = mecHost;

            MecPlatformManager *mecpm = dynamic_cast<MecPlatformManager *>(mecHost->getSubmodule("mecPlatformManager"));
            if (mecpm == nullptr) {
                throw cRuntimeError("check_and_cast failed: cannot cast submodule 'mecPlatformManager' to MecPlatformManager");
            }
            auto mecServices = mecpm->getAvailableMecServices();
            std::string serviceName;

            /* I assume the app requires only one mec service */
            if (appDesc.getAppServicesRequired().size() > 0)
            {
                serviceName = appDesc.getAppServicesRequired()[0];
            }
            else
            {
                break;
            }
            auto it = mecServices->begin();
            for (; it != mecServices->end(); ++it)
            {
                if (serviceName.compare(it->getName()) == 0)
                {
                    bestHost = mecHost;
                    break;
                }
            }
        }
        if (bestHost != nullptr)
            EV << "MecOrchestrator::findBestMecHost - best MEC host: " << bestHost << endl;
        else
            EV << "MecOrchestrator::findBestMecHost - no MEC host found" << endl;

        return bestHost;
    }

    void MecOrchestrator::getConnectedMecHosts()
    {
        // getting the list of mec hosts associated to this mec system from parameter
        if (this->hasPar("mecHostList") && strcmp(par("mecHostList").stringValue(), ""))
        {
            std::string mecHostList = par("mecHostList").stdstringValue();
            EV << "MecOrchestrator::getConnectedMecHosts - mecHostList: " << par("mecHostList").stringValue() << endl;
            char *token = strtok((char *)mecHostList.c_str(), ", "); // split by commas

            while (token != NULL)
            {
                EV << "MecOrchestrator::getConnectedMecHosts - mec host (from par): " << token << endl;
                cModule *mecHostModule = getSimulation()->getModuleByPath(token);
                mecHosts.push_back(mecHostModule);
                // PERFORMANCE IMPROVEMENT: Build index for O(1) lookup by name
                mecHostIndex_[mecHostModule->getName()] = mecHostModule;
                token = strtok(NULL, ", ");
            }
        }
        else
        {
            //        throw cRuntimeError ("MecOrchestrator::getConnectedMecHosts - No mecHostList found");
            EV << "MecOrchestrator::getConnectedMecHosts - No mecHostList found" << endl;
        }
    }

    void MecOrchestrator::registerMecService(ServiceDescriptor &serviceDescriptor) const
    {
        EV << "MecOrchestrator::registerMecService - Registering MEC service [" << serviceDescriptor.name << "]" << endl;
        for (auto mecHost : mecHosts)
        {
            cModule *module = mecHost->getSubmodule("mecPlatform")->getSubmodule("serviceRegistry");
            if (module != nullptr)
            {
                EV << "MecOrchestrator::registerMecService - Registering MEC service [" << serviceDescriptor.name << "] in MEC host [" << mecHost->getName() << "]" << endl;
                ServiceRegistry *serviceRegistry = check_and_cast<ServiceRegistry *>(module);
                serviceRegistry->registerMecService(serviceDescriptor);
            }
        }
    }

    void MecOrchestrator::removeAppFromSystem(std::string ueAddress, std::string oldMEHId)
    {
        std::string ueIp = ueAddress;
        if (ueAddress.find("acr:") == 0) {
            ueIp = ueAddress.substr(4);
        }
        //std::cout << simTime() << " [MEO] App REMOVE requested for UE: " << ueIp << std::endl;
        inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

        // remove user from the userMEHMap
        userMEHMap.erase(ueAddress);

        // requestId will be zero so the UALCMP will not try to send a response to the UE when receives the ack from the MEO
        int requestId = 0; // I've changed the starting value of the request counter to 1

        // NEW - Search with IP (without prefix)
        auto result = mecAppRegistry_->findAppByUeAddress(ueIp);
        if(!result.found)
        {
            //std::cout << "RemoveOnExit::reactOnUpdate - ERROR: contextId not found for ueAddress " << ueIp << endl;
            return;
        }
        int contextId = result.contextId;


        if (contextId == -1)
        {
            std::cout << "RemoveOnExit::reactOnUpdate - ERROR: -1.. contextId not found for ueAddress " << ueIp << endl;
            return;
        }

        // create the message to stop the MEC app
        DeleteContextAppMessage *msg = new DeleteContextAppMessage("DeleteContextAppMessage");
        msg->setType(DELETE_CONTEXT_APP);
        msg->setRequestId(requestId);
        msg->setContextId(contextId);

        // invoke stopMECApp method from MecOrchestrator
        EV << "RemoveOnExit::reactOnUpdate - sending DeleteContextAppMessage to MecOrchestrator to stop MEC app with contextId " << contextId << endl;
        mecAppLifecycleManager_->stopApplication(msg);
        delete msg;
    }

    MigrationResult MecOrchestrator::migrateApp(std::string ueAddress, std::string newMEHId, std::string oldMEHId) {
        return mecAppMigrationManager_->migrateApp(ueAddress, newMEHId, oldMEHId);
    }

    MigrationResult MecOrchestrator::checkIfMigrationIsNeeded(std::string ueAddress, std::string newMEHId, std::string oldMEHId) {
        return mecAppMigrationManager_->checkIfMigrationIsNeeded(ueAddress, newMEHId, oldMEHId);
    }

    MigrationResult MecOrchestrator::completeMigration(UALCMPMessage* ackMsg) {
        return mecAppMigrationManager_->completeMigration(ackMsg);
    }

    std::string MecOrchestrator::getAppCurrentMEH(std::string ueAddress) {
        return mecAppMigrationManager_->getAppCurrentMEH(ueAddress);
    }

    const ApplicationDescriptor* MecOrchestrator::getApplicationDescriptorByAppName(std::string& appName) const
    {
        if(!mecAppLifecycleManager_)
        {
            EV << "MecOrchestrator::getApplicationDescriptorByAppName - ERROR: mecAppLifecycleManager_ not found" << endl;
            return nullptr;
        }
        const ApplicationDescriptor* appDescriptor = mecAppLifecycleManager_->getApplicationDescriptorByAppName(appName);
        return appDescriptor ? appDescriptor : nullptr;
    }

    const std::map<std::string, ApplicationDescriptor>* MecOrchestrator::getAllApplicationDescriptors() const
    {
        if(!mecAppLifecycleManager_)
        {
            EV << "MecOrchestrator::getAllApplicationDescriptors - ERROR: mecAppLifecycleManager_ not found" << endl;
            return nullptr;
        }
        return mecAppLifecycleManager_->getAllApplicationDescriptors();
    }
} // namespace
