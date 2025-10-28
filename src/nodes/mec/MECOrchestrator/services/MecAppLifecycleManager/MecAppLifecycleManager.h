#ifndef __MECAPPLIFECYCLEMANAGER_H_
#define __MECAPPLIFECYCLEMANAGER_H_

#include "nodes/mec/MECOrchestrator/ApplicationDescriptor/ApplicationDescriptor.h"
#include "nodes/mec/MECOrchestrator/services/MecAppRegistry/MecAppRegistry.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/CreateContextAppMessage.h"
#include "nodes/mec/UALCMP/UALCMPMessages/CreateContextAppAckMessage.h"
#include <map>
#include <string>

namespace simu5g {
using namespace omnetpp;

class MecPlatformManager;
class VirtualisationInfrastructureManager;
class SelectionPolicyBase;

struct LifecycleResult {
    bool success;
    std::string errorMessage;
    int contextId;
    double processingTime;

    LifecycleResult(bool success, const std::string& errorMessage, int contextId, double processingTime) 
    : success(success), errorMessage(errorMessage), contextId(contextId), processingTime(processingTime) {}

    LifecycleResult() : success(false), contextId(-1), processingTime(0.0) {}
};

class MecAppLifecycleManager {
public:
    MecAppLifecycleManager(MecAppRegistry* mecAppRegistry, SelectionPolicyBase* hostSelectionPolicy);
    virtual ~MecAppLifecycleManager() = default;

    void initialize(double onboardingTime, double instantiationTime, double terminationTime);

    const ApplicationDescriptor& onboardApplicationPackage(const char* fileName); 
    void onboardApplicationPackages(const std::string& appList);

    const ApplicationDescriptor& getApplicationDescriptor(const std::string& appDId) const;
    const std::map<std::string, ApplicationDescriptor>* getAllApplicationDescriptors() const;

    LifecycleResult startApplication(UALCMPMessage* msg);
    LifecycleResult stopApplication(UALCMPMessage* msg);
    LifecycleResult stopApplication(int contextId);

    double getOnboardingTime() const { return onboardingTime_; }
    double getInstantiationTime() const { return instantiationTime_; }
    double getTerminationTime() const { return terminationTime_; }

    int getNextContextId() { return contextIdCounter_++; }

private:

    MecAppRegistry* mecAppRegistry_;
    SelectionPolicyBase* hostSelectionPolicy_;
    
    double onboardingTime_;
    double instantiationTime_;
    double terminationTime_;
    
    int contextIdCounter_;

    std::map<std::string, ApplicationDescriptor> applicationDescriptors_;

    // Private helper methods
    LifecycleResult instantiateApplication(const ApplicationDescriptor& desc, CreateContextAppMessage* msg, cModule* selectedHost);
    LifecycleResult terminateApplication(int contextId);
};

} // namespace simu5g

#endif