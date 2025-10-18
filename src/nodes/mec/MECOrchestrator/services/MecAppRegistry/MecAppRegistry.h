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

        // Default constructor
        AppEntry() : contextId(-1), mecUeAppID(-1), mecHost(nullptr), vim(nullptr), 
              mecpm(nullptr), uePort(0), mecAppPort(0), isEmulated(false), lastAckStartSeqNum(0), lastAckStopSeqNum(0) {}
        
        // Constructor for easy initialization
        AppEntry(int id, const std::string& appId, const std::string& name, int ueId, cModule* host, const inet::L3Address& addr): 
              contextId(id), appDId(appId), mecAppName(name), mecUeAppID(ueId), mecHost(nullptr), vim(nullptr), mecpm(nullptr),
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
    
    // App lookup methods - return AppLookupResult
    AppLookupResult findAppByUeAddress(const std::string& ueAddress);
    AppLookupResult findAppByContextId(int contextId);
    bool isAppAlreadyRunning(int ueAppId, const std::string& appDId);
    
    // App management methods
    bool registerApp(const AppEntry& appEntry);
    bool unregisterApp(int contextId);
    bool updateApp(int contextId, const AppEntry& appEntry);
    
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

} // namespace simu5g

#endif