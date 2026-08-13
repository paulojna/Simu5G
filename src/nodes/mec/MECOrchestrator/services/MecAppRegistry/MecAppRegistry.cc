#include "nodes/mec/MECOrchestrator/services/MecAppRegistry/MecAppRegistry.h"
#include "inet/networklayer/common/L3Address.h"

namespace simu5g {

MecAppRegistry::AppLookupResult MecAppRegistry::findAppByContextId(int contextId) 
{
    EV << "MecAppRegistry::findAppByContextId - Searching for app with contextId: " << contextId << endl;
    
    auto it = appMap_.find(contextId);
    if(it != appMap_.end())
    {
        // Gone entries are kept for the record but are not live apps; every
        // caller of the find functions means a live app, so a Gone entry is
        // reported as not found.
        if(it->second.state == AppState::Gone)
        {
            EV << "MecAppRegistry::findAppByContextId - App with contextId " << contextId << " is gone" << endl;
            return AppLookupResult(false, -1, nullptr);
        }

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
            // Gone entries are kept for the record but are not live apps; a
            // user whose app was deleted has no app, and every caller here
            // means a live one.
            if (appIt->second.state == AppState::Gone) {
                EV << "MecAppRegistry::findAppByUeAddress - App for UE address " << ueAddress << " is gone" << endl;
                return AppLookupResult();
            }

            EV << "MecAppRegistry::findAppByUeAddress - Found app: " << appIt->second.mecAppName
               << " (contextId: " << contextId << ", appDId: " << appIt->second.appDId << ")" << endl;
            return AppLookupResult(true, contextId, &appIt->second);
        }
    }

    EV << "MecAppRegistry::findAppByUeAddress - No app found for UE address: " << ueAddress << endl;
    return AppLookupResult();
}

MecAppRegistry::AppLookupResult MecAppRegistry::findAppByUeAppId(int mecUeAppID, const std::string& appDId)
{
    // Linear scan on purpose: mecUeAppID has no index, and the map holds one
    // entry per user ever seen — tens of rows at these scales.
    for (auto& pair : appMap_) {
        if (pair.second.mecUeAppID == mecUeAppID && pair.second.appDId == appDId
                && pair.second.state != AppState::Gone) {
            return AppLookupResult(true, pair.first, &pair.second);
        }
    }
    return AppLookupResult();
}

bool MecAppRegistry::isAppAlreadyRunning(int ueAppId, const std::string& appDId)
{
    return findAppByUeAppId(ueAppId, appDId).found;
}

bool MecAppRegistry::setAppState(int contextId, AppState state)
{
    auto it = appMap_.find(contextId);
    if (it == appMap_.end()) {
        EV << "MecAppRegistry::setAppState - contextId " << contextId << " not found" << endl;
        return false;
    }
    if (it->second.state == AppState::Gone) {
        EV << "MecAppRegistry::setAppState - contextId " << contextId
           << " is gone; refusing to change its state" << endl;
        return false;
    }
    it->second.state = state;
    return true;
}

bool MecAppRegistry::recordMigration(int contextId, cModule* newHost, const MecAppInstanceInfo* appInfo)
{
    auto it = appMap_.find(contextId);
    if (it == appMap_.end() || it->second.state == AppState::Gone) {
        EV << "MecAppRegistry::recordMigration - no live app with contextId " << contextId << endl;
        return false;
    }

    AppEntry& entry = it->second;
    entry.mecHost = newHost;
    entry.vim = newHost->getSubmodule("vim");
    entry.mecpm = newHost->getSubmodule("mecPlatformManager");
    entry.updateFromInstanceInfo(appInfo);

    EV << "MecAppRegistry::recordMigration - contextId " << contextId << " now on "
       << newHost->getName() << " at " << entry.getEndpointString() << endl;
    return true;
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
    if(it != appMap_.end() && it->second.state != AppState::Gone)
    {
        EV << "MecAppRegistry::unregisterApp - Found app to unregister: " << it->second.mecAppName
           << " (appDId: " << it->second.appDId << ", UE address: " << it->second.ueAddress.str() << ")" << endl;

        // The app module is destroyed by the caller; the entry stays as the
        // record that this user had an app and when it went. The address index
        // keeps pointing here — the find functions treat Gone as not found,
        // and a later registration for the same address overwrites the index.
        it->second.state = AppState::Gone;
        return true;
    }

    EV << "MecAppRegistry::unregisterApp - App with contextId " << contextId << " not found or already gone" << endl;
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