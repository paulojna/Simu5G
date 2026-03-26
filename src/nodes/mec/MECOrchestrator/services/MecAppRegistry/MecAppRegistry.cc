#include "nodes/mec/MECOrchestrator/services/MecAppRegistry/MecAppRegistry.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/networklayer/common/L3AddressResolver.h"

namespace simu5g {

MecAppRegistry::AppLookupResult MecAppRegistry::findAppByContextId(int contextId) 
{
    EV << "MecAppRegistry::findAppByContextId - Searching for app with contextId: " << contextId << endl;
    
    auto it = appMap_.find(contextId);
    if(it != appMap_.end())
    {
        EV << "MecAppRegistry::findAppByContextId - Found app: " << it->second.mecAppName 
           << " (appDId: " << it->second.appDId << ", UE address: " << it->second.ueAddress.str() << ")" << endl;
        return AppLookupResult(true, contextId, &it->second);
    }
    
    EV << "MecAppRegistry::findAppByContextId - App with contextId " << contextId << " not found" << endl;
    return AppLookupResult(false, -1, nullptr);
}

MecAppRegistry::AppLookupResult MecAppRegistry::findAppByUeAddress(const std::string& ueAddress)
{
    EV << "MecAppRegistry::findAppByUeAddress - Searching for app with UE address: " << ueAddress << endl;

    // PERFORMANCE IMPROVEMENT: Use existing ueAddressToContextId_ index for O(1) lookup
    // instead of O(n) linear search + L3AddressResolver overhead
    // Original code commented out for reference:
    // inet::L3Address ueL3Address = inet::L3AddressResolver().resolve(ueAddress.c_str());
    // for(auto& pair: appMap_) {
    //     if(pair.second.ueAddress == ueL3Address) {
    //         return AppLookupResult(true, pair.first, &pair.second);
    //     }
    // }

    auto indexIt = ueAddressToContextId_.find(ueAddress);
    if (indexIt != ueAddressToContextId_.end()) {
        int contextId = indexIt->second;
        auto appIt = appMap_.find(contextId);
        if (appIt != appMap_.end()) {
            EV << "MecAppRegistry::findAppByUeAddress - Found app: " << appIt->second.mecAppName
               << " (contextId: " << contextId << ", appDId: " << appIt->second.appDId << ")" << endl;
            return AppLookupResult(true, contextId, &appIt->second);
        }
    }

    EV << "MecAppRegistry::findAppByUeAddress - No app found for UE address: " << ueAddress << endl;
    return AppLookupResult();
}

bool MecAppRegistry::isAppAlreadyRunning(int ueAppId, const std::string& appDId) 
{
    return appMap_.find(ueAppId) != appMap_.end() && appMap_[ueAppId].appDId == appDId;
}

void MecAppRegistry::updateUeAddressMapping(int contextId, const std::string& ueAddress) {
    EV << "MecAppRegistry::updateUeAddressMapping - Adding mapping: UE address " << ueAddress 
       << " -> contextId " << contextId << endl;
    ueAddressToContextId_[ueAddress] = contextId;
}

void MecAppRegistry::removeUeAddressMapping(const std::string& ueAddress) {
    EV << "MecAppRegistry::removeUeAddressMapping - Removing mapping for UE address: " << ueAddress << endl;
    
    auto it = ueAddressToContextId_.find(ueAddress);
    if(it != ueAddressToContextId_.end()) {
        EV << "MecAppRegistry::removeUeAddressMapping - Successfully removed mapping for UE address: " 
           << ueAddress << " (contextId: " << it->second << ")" << endl;
        ueAddressToContextId_.erase(it);
    } else {
        EV << "MecAppRegistry::removeUeAddressMapping - ERROR: UE address " << ueAddress 
           << " not found in the registry" << endl;
        throw cRuntimeError("UeAddress %s not found in the registry", ueAddress.c_str());
    }
}

bool MecAppRegistry::registerApp(const AppEntry& appEntry) 
{
    EV << "MecAppRegistry::registerApp - Attempting to register app with contextId: " << appEntry.contextId 
       << ", appDId: " << appEntry.appDId << ", mecAppName: " << appEntry.mecAppName << endl;
    
    // Check if the app is already exists
    if(appMap_.find(appEntry.contextId) != appMap_.end()) 
    {
        EV << "MecAppRegistry::registerApp - App with contextId " << appEntry.contextId << " already exists" << endl;
        return false;
    }

    // Check if the app is already running
    if(isAppAlreadyRunning(appEntry.mecUeAppID, appEntry.appDId)) 
    {
        EV << "MecAppRegistry::registerApp - App with mecUeAppID " << appEntry.mecUeAppID 
           << " and appDId " << appEntry.appDId << " is already running" << endl;
        return false;
    }

    // Register the app
    appMap_[appEntry.contextId] = appEntry;
    updateUeAddressMapping(appEntry.contextId, appEntry.ueAddress.str());
    
    EV << "MecAppRegistry::registerApp - Successfully registered app with contextId: " << appEntry.contextId 
       << ", UE address: " << appEntry.ueAddress.str() << ", total apps: " << appMap_.size() << endl;
    return true;
}

bool MecAppRegistry::unregisterApp(int contextId) 
{
    EV << "MecAppRegistry::unregisterApp - Attempting to unregister app with contextId: " << contextId << endl;
    
    auto it = appMap_.find(contextId);
    if(it != appMap_.end()) 
    {
        EV << "MecAppRegistry::unregisterApp - Found app to unregister: " << it->second.mecAppName 
           << " (appDId: " << it->second.appDId << ", UE address: " << it->second.ueAddress.str() << ")" << endl;
        
        removeUeAddressMapping(it->second.ueAddress.str());
        appMap_.erase(it);
        return true;
    }
    
    EV << "MecAppRegistry::updateApp - App with contextId " << contextId << " not found for update" << endl;
    return false;
}

bool MecAppRegistry::updateApp(int contextId, const AppEntry& appEntry) 
{
    EV << "MecAppRegistry::updateApp - Attempting to update app with contextId: " << contextId << endl;
    
    auto it = appMap_.find(contextId);
    if(it != appMap_.end()) {
        EV << "MecAppRegistry::updateApp - Found existing app: " << it->second.mecAppName 
           << " (appDId: " << it->second.appDId << "), updating with new data" << endl;
        
        if(it->second.ueAddress.str() != appEntry.ueAddress.str()) {
            removeUeAddressMapping(it->second.ueAddress.str());
            updateUeAddressMapping(contextId, appEntry.ueAddress.str());
        }
        
        it->second = appEntry;
        
        EV << "MecAppRegistry::updateApp - Successfully updated app with contextId: " << contextId 
           << ", new app name: " << appEntry.mecAppName << endl;
        return true;
    }
    
    EV << "MecAppRegistry::updateApp - App with contextId " << contextId << " not found for update" << endl;
    return false;
}

MecAppRegistry::AppEntry MecAppRegistry::createAppEntry(int contextId, const std::string& appDId, const std::string& mecAppName, int mecUeAppID, cModule* mecHost, const inet::L3Address& ueAddress)
{
    EV << "MecAppRegistry::createAppEntry - Creating new app entry with contextId: " << contextId
       << ", appDId: " << appDId << ", UE address: " << ueAddress.str() << endl;
    
    return AppEntry(contextId, appDId, mecAppName, mecUeAppID, mecHost, ueAddress);
}

void MecAppRegistry::printRegistry() const {
    EV << "MecAppRegistry::printRegistry - Current registry state:" << endl;
    EV << "  Total apps registered: " << appMap_.size() << endl;
    
    if (appMap_.empty()) {
        EV << "  Registry is empty" << endl;
        return;
    }
    
    for (const auto& pair : appMap_) {
        const AppEntry& entry = pair.second;
        EV << " ContextId: " << pair.first << ", AppDId: " << entry.appDId << ", MecAppName: " << entry.mecAppName 
           << ", MecUeAppID: " << entry.mecUeAppID << ", UE address: " << entry.ueAddress.str() << 
           ", Endpoint: " << entry.getEndpointString() << endl;
    }
}

} // namespace simu5g