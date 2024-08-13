#include "LocationSelectionBased.h"
#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

namespace simu5g {

cModule* LocationSelectionBased::findBestMecHost(const ApplicationDescriptor &appDesc, inet::L3Address ueAddress)
{
    EV << "LocationSelectionBased::findBestMecHost - finding best MecHost..." << endl;
    cModule *bestHost = nullptr;

    // copy userMEHMap to a local variable
    std::map<std::string, std::pair<std::string, std::string>> userMEHMapCopy = mecOrchestrator_->userMEHMap;

    std::string ueToFind = "acr:"+ueAddress.str();
    auto it = userMEHMapCopy.find(ueToFind);
    if(it == userMEHMapCopy.end())
    {
        EV << "LocationSelectionBased::findBestMecHost - We don't have information on [" << ueToFind << "]. Searching for the best MEC host on a AvailableResourcesBased strategy"<< endl;
        double maxCpuSpeed = -1;

        for(auto mecHost : mecOrchestrator_->mecHosts)
        {
            VirtualisationInfrastructureManager *vim = check_and_cast<VirtualisationInfrastructureManager *>(mecHost->getSubmodule("vim"));
            ResourceDescriptor resources = appDesc.getVirtualResources();
            bool res = vim->isAllocable(resources.ram, resources.disk, resources.cpu);
            if (!res)
            {
                EV << "LocationSelectionBased::findBestMecHost - MEC host [" << mecHost->getName() << "] has not got enough resources. Searching again..." << endl;
                continue;
            }
            if (vim->getAvailableResources().cpu > maxCpuSpeed)
            {
                // Temporally select this mec host as the best
                EV << "LocationSelectionBased::findBestMecHost - MEC host [" << mecHost->getName() << "] temporally chosen as bet MEC host. Available resources: " << endl;
                vim->printResources();
                bestHost = mecHost;
                maxCpuSpeed = vim->getAvailableResources().cpu;
            }
        }
    }
    else
    {
        EV << "LocationSelectionBased::findBestMecHost - We have information on [" << ueToFind << "]. Searching for the best MEC host on a LocationSelectionBased strategy" << endl;
        // find the mecHost on mecHosts
        for(auto mecHost : mecOrchestrator_->mecHosts)
        {
            if(mecHost->getName() == it->second.second)
            {
                EV << "LocationSelectionBased::findBestMecHost - MEC host ["<< mecHost->getName() << "] has been chosen as the best Mec Host" << endl;
                bestHost = mecHost;
                break;
            }
        }
    }

    if(bestHost != nullptr)
        EV << "LocationSelectionBased::findBestMecHost - MEC host ["<< bestHost->getName() << "] has been chosen as the best Mec Host" << endl;
    else
        EV << "LocationSelectionBased::findBestMecHost - No Mec Host found" << endl;
    
    return bestHost;
}

}