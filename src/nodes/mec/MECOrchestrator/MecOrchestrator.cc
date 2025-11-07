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

#define USERS_UPDATE 7
#define USERS_ENTRY 8

    Define_Module(MecOrchestrator);

    MecOrchestrator::MecOrchestrator()
    {
        //meAppMap.clear();
        //mecApplicationDescriptors_.clear();
        mecHostSelectionPolicy_ = nullptr;
        userMEHMap.clear();
        reactionOnUpdate_ = nullptr;
        // NEW
        mecAppRegistry_ = nullptr;
        mecAppLifecycleManager_ = nullptr;
        mecAppMigrationManager_ = nullptr;
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
              this
            );
            mecAppMigrationManager_->initialize(migrationTime_, migrationTimeout_);

            reactionOnUpdate_ = new MigrateOnChange(static_cast<IOrchestratorApi *>(this));
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
            auto received_packet = packet->peekAtFront<RavensControllerUpdatePacket>();
            std::vector<UserMEHUpdate> UserMEHUpdatedList_toPrint;
            if (received_packet->getType() == USERS_UPDATE)
            {
                auto usersUpdate = packet->peekAtFront<UserMEHUpdatedListMessage>();
                UserMEHUpdatedList_toPrint = usersUpdate->getUeMehList();
                for (auto user : UserMEHUpdatedList_toPrint)
                {
                    EV << "MecOrchestrator::socketDataArrived - user address: " << user.getAddress() << " last MEH: " << user.getLastMEHId() << " new MEH: " << user.getNewMEHId() << endl;
                    userMEHMap[user.getAddress()] = {user.getAddress(), user.getNewMEHId()};
                     reactionOnUpdate_->reactOnUpdate(user);
                    EV << "MecOrchestrator::socketDataArrived - reactOnUpdate done!" << endl;
                }
            }
            else if (received_packet->getType() == USERS_ENTRY)
            {
                auto usersEntry = packet->peekAtFront<UserEntryListMessage>();
                std::vector<UserEntryUpdate> UserEntryList_toPrint = usersEntry->getUeEntryList();
                reactionOnUpdate_->reactOnUpdate(UserEntryList_toPrint);
                for (auto user : UserEntryList_toPrint)
                {
                    userMEHMap[user.getAddress()] = {user.getAddress(), user.getCurrentMEHId()};
                }
            }
        }

        delete msg;
        return;
    }

    nlohmann::json MecOrchestrator::formatDataFromRAVENS(std::vector<UserEntryUpdate> UserEntryUpdatedList)
    {
        nlohmann::json jsonList;
        // lets create a list of json objects, each one representing a user
        for (auto user : UserEntryUpdatedList)
        {
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

    void MecOrchestrator::handleUALCMPMessage(cMessage *msg)
    {
        UALCMPMessage *lcmMsg = safe_check_and_cast<UALCMPMessage>(msg);

        /* Handling CREATE_CONTEXT_APP */
        if (!strcmp(lcmMsg->getType(), CREATE_CONTEXT_APP))
        {
            LifecycleResult result = mecAppLifecycleManager_->startApplication(lcmMsg);
            if(result.success) {
                std::cout << "MecOrchestrator::handleUALCMPMessage - CREATE_CONTEXT_APP success, contextId: " << result.contextId << std::endl;
                sendCreateAppContextAck(true, lcmMsg->getRequestId(), result.contextId);
            }
            else {
                std::cout << "MecOrchestrator::handleUALCMPMessage - CREATE_CONTEXT_APP failed, contextId: " << result.contextId << std::endl;
                sendCreateAppContextAck(false, lcmMsg->getRequestId());
            }
        }
        /* Handling DELETE_CONTEXT_APP */
        else if (!strcmp(lcmMsg->getType(), DELETE_CONTEXT_APP))
        {
            LifecycleResult result = mecAppLifecycleManager_->stopApplication(lcmMsg);
            if(result.success) {
                std::cout << "MecOrchestrator::handleUALCMPMessage - DELETE_CONTEXT_APP success, contextId: " << result.contextId << std::endl;
                sendDeleteAppContextAck(true, lcmMsg->getRequestId(), result.contextId);
            }
            else {
                std::cout << "MecOrchestrator::handleUALCMPMessage - DELETE_CONTEXT_APP failed, contextId: " << result.contextId << std::endl;
                sendDeleteAppContextAck(false, lcmMsg->getRequestId());
            }
        }
        /* Handling confirmation of MEH change*/
        else if (!strcmp(lcmMsg->getType(), ACK_UPDATE_MEH_IP))
        {
            if (!mecAppMigrationManager_) 
            {
                EV << "MecOrchestrator::handleUALCMPMessage - Migration manager not initialized" << endl;
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

    // Callback function to write response data
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        ((std::string *)userp)->append((char *)contents, size * nmemb);
        return size * nmemb;
    }

    std::string MecOrchestrator::postRequestPrediction(const std::string &url, const nlohmann::json &jsonObject)
    {
        CURL *curl;
        CURLcode res;
        std::string response;

        curl = curl_easy_init();
        if (!curl)
        {
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
        if (res != CURLE_OK)
        {
            EV << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
        }
        else
        {
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

    void MecOrchestrator::removeAppFromSystem(std::string ueAddress, std::string oldMEHId)
    {
        std::string ueIp = ueAddress.substr(4);
        inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueIp.c_str());

        // remove user from the userMEHMap
        userMEHMap.erase(ueAddress);

        // requestId will be zero so the UALCMP will not try to send a response to the UE when receives the ack from the MEO
        int requestId = 0; // I've changed the starting value of the request counter to 1

        // NEW
        auto result = mecAppRegistry_->findAppByUeAddress(ueAddress);
        if(!result.found)
        {
            EV << "RemoveOnExit::reactOnUpdate - ERROR: contextId not found for ueAddress " << ueAddress << endl;
            return;
        }
        int contextId = result.contextId;


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
        mecAppLifecycleManager_->stopApplication(msg);
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
