#ifndef __MECAPPREGISTRY_H_
#define __MECAPPREGISTRY_H_

#include "inet/networklayer/common/L3Address.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"  
#include <map>
#include <string>

namespace simu5g {

using namespace omnetpp;

class MecAppRegistry {
public:
    // Lifecycle state of an application. Placed on creation; the migration and
    // teardown flows move it through the other states. Gone entries are kept,
    // not erased: that this user had an app, and when it went, is what offline
    // evaluation joins against.
    enum class AppState { Placed, Migrating, AwaitingConfirmation, Gone };

    // Internal data structure - encapsulates all app information
    struct AppEntry {
        int contextId;
        std::string appDId;
        std::string mecAppName;
        std::string mecAppInstanceId;
        int mecUeAppID;
        cModule* mecHost;
        cModule* vim;
        cModule* mecpm;
        
        std::string ueSymbolicAddress;
        inet::L3Address ueAddress;
        int uePort;
        
        // Network endpoint information (from MecAppInstanceInfo)
        inet::L3Address mecAppAddress;
        int mecAppPort;
        
        bool isEmulated;
        
        int lastAckStartSeqNum;
        int lastAckStopSeqNum;

        AppState state = AppState::Placed;

        // Default constructor
        AppEntry() : contextId(-1), mecUeAppID(-1), mecHost(nullptr), vim(nullptr), 
              mecpm(nullptr), uePort(0), mecAppPort(0), isEmulated(false), lastAckStartSeqNum(0), lastAckStopSeqNum(0) {}
        
        // Constructor for easy initialization
        AppEntry(int id, const std::string& appId, const std::string& name, int ueId, cModule* host, const inet::L3Address& addr): 
              contextId(id), appDId(appId), mecAppName(name), mecUeAppID(ueId), mecHost(host), vim(nullptr), mecpm(nullptr),
              ueAddress(addr), uePort(0), mecAppPort(0), isEmulated(false), lastAckStartSeqNum(0), lastAckStopSeqNum(0) {
                if(host)
                {
                    vim = host->getSubmodule("vim");
                    mecpm = host->getSubmodule("mecPlatformManager");
                }
              }
              
        // Update from MecAppInstanceInfo (after successful instantiation)
        void updateFromInstanceInfo(const MecAppInstanceInfo* appInfo) {
            if (appInfo && appInfo->status) {
                mecAppAddress = appInfo->endPoint.addr;
                mecAppPort = appInfo->endPoint.port;
                mecAppInstanceId = appInfo->instanceId;
            }
        }
        
        // Get endpoint as string
        std::string getEndpointString() const {
            return mecAppAddress.str() + ":" + std::to_string(mecAppPort);
        }
    };
    
    // Simple lookup result structure
    struct AppLookupResult {
        bool found;
        int contextId;
        const AppEntry* appEntry;  // Read-only access
        
        AppLookupResult() : found(false), contextId(-1), appEntry(nullptr) {}
        AppLookupResult(bool f, int id, const AppEntry* entry) 
            : found(f), contextId(id), appEntry(entry) {}
    };
    
    // Constructor/Destructor
    MecAppRegistry() = default;
    ~MecAppRegistry() = default;
    
    // App lookup methods - return AppLookupResult. One lookup per key space —
    // contextId, canonical UE address, mecUeAppID — because a value from one
    // ID space must never probe a map keyed by another. All of them treat
    // Gone entries as not found: every caller means a live app.
    AppLookupResult findAppByUeAddress(const std::string& ueAddress);
    AppLookupResult findAppByContextId(int contextId);
    AppLookupResult findAppByUeAppId(int mecUeAppID, const std::string& appDId);
    bool isAppAlreadyRunning(int ueAppId, const std::string& appDId);

    // App management methods
    bool registerApp(const AppEntry& appEntry);
    bool unregisterApp(int contextId);
    bool updateApp(int contextId, const AppEntry& appEntry);

    // Lifecycle-state writers — lifecycle and migration code only; strategies
    // read, never write. setAppState refuses to move an entry out of Gone:
    // nothing may resurrect a deleted app in this round.
    bool setAppState(int contextId, AppState state);

    // Records a migration the manager performed — bookkeeping only, nothing
    // is instantiated or destroyed here. Migration rewrites where the app
    // runs, never which app it is: same entry, same contextId, new host and
    // endpoint. The platform-side instance is new; the orchestrator's record
    // keeps its identity, which retention and the decision-log join depend on.
    bool recordMigration(int contextId, cModule* newHost, const MecAppInstanceInfo* appInfo);
    
    // App creation helper
    AppEntry createAppEntry(int contextId, const std::string& appDId, const std::string& mecAppName, int mecUeAppID, cModule* mecHost, const inet::L3Address& ueAddress);
    
    // Getters
    size_t getAppCount() const { return appMap_.size(); }
    bool isEmpty() const { return appMap_.empty(); }
    
    // Debug/utility methods
    void printRegistry() const;
    
    // Iterator support for range-based loops
    typedef std::map<int, AppEntry>::const_iterator const_iterator;
    const_iterator begin() const { return appMap_.begin(); }
    const_iterator end() const { return appMap_.end(); }

private:
    std::map<int, AppEntry> appMap_;
    std::map<std::string, int> ueAddressToContextId_;
    
    void updateUeAddressMapping(int contextId, const std::string& ueAddress);
    void removeUeAddressMapping(const std::string& ueAddress);
};

// The single spelling of each state, for logs and for the learning engine's
// observation — the same arrangement as userEventTypeName for events. A state
// written differently in two places is a state that cannot be joined across two
// files.
inline const char *appStateName(MecAppRegistry::AppState state)
{
    switch (state) {
        case MecAppRegistry::AppState::Placed:               return "Placed";
        case MecAppRegistry::AppState::Migrating:            return "Migrating";
        case MecAppRegistry::AppState::AwaitingConfirmation: return "AwaitingConfirmation";
        case MecAppRegistry::AppState::Gone:                 return "Gone";
    }
    return "Unknown";
}

} // namespace simu5g

#endif