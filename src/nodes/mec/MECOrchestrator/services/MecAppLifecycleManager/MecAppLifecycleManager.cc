#include "nodes/mec/MECOrchestrator/services/MecAppLifecycleManager/MecAppLifecycleManager.h"
#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/SelectionPolicyBase.h"
#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"
#include "common/binder/Binder.h"
#include "inet/networklayer/common/L3AddressResolver.h"

namespace simu5g {

using namespace omnetpp;

MecAppLifecycleManager::MecAppLifecycleManager(MecAppRegistry* mecAppRegistry, SelectionPolicyBase* hostSelectionPolicy)
: mecAppRegistry_(mecAppRegistry), hostSelectionPolicy_(hostSelectionPolicy) {
}

void MecAppLifecycleManager::initialize(double onboardingTime, double instantiationTime, double terminationTime) {
    onboardingTime_ = onboardingTime;
    instantiationTime_ = instantiationTime;
    terminationTime_ = terminationTime;
    contextIdCounter_ = 0;
}

/*
    This method is used to onboard an application package.
    It creates a new ApplicationDescriptor object and adds it to the applicationDescriptors_ map.
    It returns the ApplicationDescriptor object.
*/
const ApplicationDescriptor& MecAppLifecycleManager::onboardApplicationPackage(const char* fileName) {
    ApplicationDescriptor desc(fileName);
    const std::string& appDId = desc.getAppDId();
    if (applicationDescriptors_.find(appDId) != applicationDescriptors_.end()) {
        EV << "Application package with ID " << appDId << " already onboarded\n";
    }
    applicationDescriptors_[appDId] = desc;
    return applicationDescriptors_[appDId];
}

/*
    Method to onboard a list of application packages. 
    It uses the onboardApplicationPackage method to onboard each package.
*/
void MecAppLifecycleManager::onboardApplicationPackages(const std::string& appList) {
    if (appList.empty()) {
        EV << "MecAppLifecycleManager::onboardApplicationPackages - No package list provided" << endl;
        //std::cout << "MecAppLifecycleManager::onboardApplicationPackages - No package list provided" << std::endl;
        return;
    }
    
    char* token = strtok((char*)appList.c_str(), ", ");
    while (token != nullptr) {
        std::string fileName = "ApplicationDescriptors/" + std::string(token) + ".json";
        //std::cout << "MecAppLifecycleManager::onboardApplicationPackages - Onboarding package: " << fileName << std::endl;
        onboardApplicationPackage(fileName.c_str());
        token = strtok(nullptr, ", ");
    }
}

/*
    Method to get an application descriptor by its appDId.
    It returns the ApplicationDescriptor object if found, otherwise an empty ApplicationDescriptor object.
*/
const ApplicationDescriptor* MecAppLifecycleManager::getApplicationDescriptor(const std::string& appDId) const {
    auto it = applicationDescriptors_.find(appDId);
    return (it != applicationDescriptors_.end()) ? &it->second : nullptr;
}

/*
    Method to get an application descriptor by its app name.
    It returns the ApplicationDescriptor object if found, otherwise nullptr.
*/
const ApplicationDescriptor* MecAppLifecycleManager::getApplicationDescriptorByAppName(const std::string& appName) const {
    auto it = applicationDescriptors_.find(appName);
    return (it != applicationDescriptors_.end()) ? &it->second : nullptr;
}

/*
    Method to get all application descriptors.
*/
const std::map<std::string, ApplicationDescriptor>* MecAppLifecycleManager::getAllApplicationDescriptors() const {
    return &applicationDescriptors_;
}

LifecycleResult MecAppLifecycleManager::startApplication(UALCMPMessage* msg) {
    CreateContextAppMessage* createContextAppMsg = check_and_cast<CreateContextAppMessage*>(msg);
    EV << "MecAppLifecycleManager::startApplication - Starting application " << createContextAppMsg->getAppDId() << endl;

    LifecycleResult result;

    // Check if the application is already running
    int ueAppID = atoi(createContextAppMsg->getDevAppId());
    if(mecAppRegistry_->isAppAlreadyRunning(ueAppID, createContextAppMsg->getAppDId())) {
        EV << "MecAppLifecycleManager::startApplication - Application " << createContextAppMsg->getAppDId() << " already running" << endl;
        auto result = mecAppRegistry_->findAppByUeAddress(createContextAppMsg->getAppDId());
        if(result.found) {
            EV << "MecAppLifecycleManager::startApplication - Application " << createContextAppMsg->getAppDId() << " found" << endl;
            return LifecycleResult(true, "Application already running", result.contextId, 0.0);
        }
        else {
            EV << "MecAppLifecycleManager::startApplication - Application " << createContextAppMsg->getAppDId() << " not found" << endl;
        }
    }

    std::string appDid;
    double processingTime = 0.0;

    if(createContextAppMsg->getOnboarded() == false) {
        const ApplicationDescriptor &appDesc = onboardApplicationPackage(createContextAppMsg->getAppPackagePath());
        appDid = appDesc.getAppDId();
        processingTime += onboardingTime_;
    }
    else {
        appDid = createContextAppMsg->getAppDId();
    }

    const ApplicationDescriptor* appDescPtr = getApplicationDescriptor(appDid);
    if(appDescPtr == nullptr || appDescPtr->getAppDId().empty()) {
        EV << "MecAppLifecycleManager::startApplication - Application package not onboarded" << endl;
        return LifecycleResult(false, "Application package not onboarded", -1, 0.0);
    }

    const ApplicationDescriptor& appDesc = *appDescPtr;

    inet::L3Address ueAddress = inet::L3AddressResolver().resolve(createContextAppMsg->getUeIpAddress());
    cModule *bestHost = hostSelectionPolicy_->findBestMecHost(appDesc, ueAddress);
    //std::cout << "MecAppLifecycleManager::startApplication - Best host found: " << bestHost->getFullPath() << std::endl;
    if(bestHost == nullptr) {
        EV << "MecAppLifecycleManager::startApplication - No best host found" << endl;
        processingTime += instantiationTime_ / 2;
        return LifecycleResult(false, "No best host found", -1, processingTime);
    }   
    else {
        LifecycleResult result = instantiateApplication(appDesc, createContextAppMsg, bestHost);
        processingTime += result.processingTime;
        return LifecycleResult(result.success, result.errorMessage, result.contextId, processingTime);
    }
}

LifecycleResult MecAppLifecycleManager::instantiateApplication(const ApplicationDescriptor& appDesc, CreateContextAppMessage* msg, cModule* bestHost) {
    CreateAppMessage *createAppMsg = new CreateAppMessage();
    createAppMsg->setUeAppID(atoi(msg->getDevAppId()));
    createAppMsg->setMEModuleName(appDesc.getAppName().c_str());
    createAppMsg->setMEModuleType(appDesc.getAppProvider().c_str());
    createAppMsg->setRequiredCpu(appDesc.getVirtualResources().cpu);
    createAppMsg->setRequiredRam(appDesc.getVirtualResources().ram);
    createAppMsg->setRequiredDisk(appDesc.getVirtualResources().disk);
    
    if(!appDesc.getOmnetppServiceRequired().empty()) {
        createAppMsg->setRequiredService(appDesc.getOmnetppServiceRequired().c_str());
    }
    else {
        createAppMsg->setRequiredService("NULL");
    }
    createAppMsg->setContextId(getContextIdCounter());

    // add the new mec app in the map structure
    MecAppRegistry::AppEntry newMecApp;
    newMecApp.appDId = appDesc.getAppDId();
    newMecApp.mecUeAppID = atoi(msg->getDevAppId());
    newMecApp.mecHost = bestHost;
    newMecApp.ueAddress = inet::L3AddressResolver().resolve(msg->getUeIpAddress());
    newMecApp.vim = bestHost->getSubmodule("vim");
    newMecApp.mecpm = bestHost->getSubmodule("mecPlatformManager");
    newMecApp.mecAppName = appDesc.getAppName().c_str();

    MecPlatformManager *mecpm = check_and_cast<MecPlatformManager *>(newMecApp.mecpm);

    //For the moment, lets not worry about emulation.
    newMecApp.isEmulated = false; 

    MecAppInstanceInfo *appInfo = mecpm->instantiateMEApp(createAppMsg);
    if(!appInfo->status) {
        return LifecycleResult(false, "Application instantiation failed", -1, getInstantiationTime());
    }

    EV << "MecAppLifecycleManager::instantiateApplication - Application " << appDesc.getAppName() << " instantiated on MEC host " << bestHost->getFullPath() << " at " << appInfo->endPoint.addr.str() << ":" << appInfo->endPoint.port << endl;

    newMecApp.mecAppAddress = appInfo->endPoint.addr;
    newMecApp.mecAppPort = appInfo->endPoint.port;
    newMecApp.mecAppInstanceId = appInfo->instanceId;   
    newMecApp.contextId = getContextIdCounter();
    incrementContextIdCounter();
   
    MecAppRegistry::AppEntry appEntry = mecAppRegistry_->createAppEntry(newMecApp.contextId, newMecApp.appDId, newMecApp.mecAppName.c_str(), newMecApp.mecUeAppID, newMecApp.mecHost, newMecApp.ueAddress);
    appEntry.updateFromInstanceInfo(appInfo);
    mecAppRegistry_->registerApp(appEntry);

    return LifecycleResult(true, "Application instantiated", newMecApp.contextId, getInstantiationTime());
}

LifecycleResult MecAppLifecycleManager::stopApplication(UALCMPMessage* msg) {
    
    DeleteContextAppMessage *contAppMsg = check_and_cast<DeleteContextAppMessage *>(msg);

    int contextId = contAppMsg->getContextId();
    return stopApplication(contextId);
}

LifecycleResult MecAppLifecycleManager::stopApplication(int contextId) {
    // Find the application
    auto result = mecAppRegistry_->findAppByContextId(contextId);
    if (!result.found) {
        return LifecycleResult(false, "MEC app not found", contextId, 0.0);
    }
    
    // Terminate the application
    return terminateApplication(contextId);
}

LifecycleResult MecAppLifecycleManager::terminateApplication(int contextId) {
    MecAppRegistry::AppLookupResult result = mecAppRegistry_->findAppByContextId(contextId);
    if(!result.found) {
        return LifecycleResult(false, "Application not found", -1, 0.0);
    }

    MecPlatformManager *mecpm = check_and_cast<MecPlatformManager *>(result.appEntry->mecpm);
    DeleteAppMessage *deleteAppMsg = new DeleteAppMessage();
    deleteAppMsg->setUeAppID(result.appEntry->mecUeAppID);

    bool isTerminated = mecpm->terminateMEApp(deleteAppMsg);
    
    if(isTerminated) {
        mecAppRegistry_->unregisterApp(result.appEntry->contextId);
        return LifecycleResult(true, "Application terminated", contextId, getTerminationTime());
    }
    else {
        return LifecycleResult(false, "Application termination failed", -1, getTerminationTime());
    }
}
} // namespace simu5g