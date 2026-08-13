#include "LocationSelectionBased.h"
#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

namespace simu5g {

cModule* LocationSelectionBased::findBestMecHost(const ApplicationDescriptor &appDesc, inet::L3Address ueAddress)
{
    EV << "LocationSelectionBased::findBestMecHost - finding best MecHost..." << endl;
    cModule *bestHost = nullptr;

    std::string ueToFind = ueAddress.str();
    auto it = mecOrchestrator_->userPresence_.find(ueToFind);

	// 1 - check if we have location information so we instantiate the app closest to the UE.
	// An exited user has a row with an empty currentMEH — no location to go by,
	// so that case falls through to the resource-based choice like an unknown user.
    if(it != mecOrchestrator_->userPresence_.end() && !it->second.currentMEH.empty())
    {
        EV << "LocationSelectionBased::findBestMecHost - [" << ueToFind << "] is under [" << it->second.currentMEH << "], trying to place the app there" << endl;
		// find the closest host
    	const std::string& closestHostName = it->second.currentMEH;

    	// now let's check if the host has enough resources
        for(auto mecHost : mecOrchestrator_->mecHosts)
        {
            if (std::string(mecHost->getName()) == closestHostName)
            {
				VirtualisationInfrastructureManager *vim = check_and_cast<VirtualisationInfrastructureManager *>(mecHost->getSubmodule("vim"));
            	const ResourceDescriptor& requiredResources = appDesc.getVirtualResources();

            	if (vim->isAllocable(requiredResources.ram, requiredResources.disk, requiredResources.cpu))
            	{
            		bestHost = mecHost;
            	}
            	else
            	{
            		// the closest host does not have resources, we should go for the resource based
            		EV << "LocationSelectionBased::findBestMecHost - " << mecHost->getName() << "did not have enough resources!";
            	}
            }
        }
    }

	if (bestHost == nullptr)
	{
		bestHost = findBestHostByResources(appDesc);
	}

    if(bestHost != nullptr)
        EV << "LocationSelectionBased::findBestMecHost - MEC host ["<< bestHost->getName() << "] has been chosen as the best Mec Host" << endl;
    else
        EV << "LocationSelectionBased::findBestMecHost - No Mec Host found" << endl;

    return bestHost;
}

/**
 * This method goes through all the available MEC hosts managed by the MEO.
 * It selects the one that has enough resources AND has the maximum ammount of CPU available.
 *
 * @param app The descriptor of the application to be deployed
 * @return A pointer to the best cModule host, or nullptr if no suitable host is found (mainly it will be the "Cloud-like Host", if available)
 */
cModule *LocationSelectionBased::findBestHostByResources(const ApplicationDescriptor &app)
{
	EV << "LocationSelectionBased::findeBestHostByResource - searching for host with the most available CPU..";
	cModule* bestHost = nullptr;
	double maxCpuAvailable = -1;

	const ResourceDescriptor& requiredResources = app.getVirtualResources();
	for (auto mecHost : mecOrchestrator_->mecHosts)
	{
		VirtualisationInfrastructureManager *vim = check_and_cast<VirtualisationInfrastructureManager *>(mecHost->getSubmodule("vim"));

		//check if there are enough resources to allocate the application - if not, get out quickly
		if (!vim->isAllocable(requiredResources.ram, requiredResources.disk, requiredResources.cpu))
		{
			continue;
		}

		double availableCPU = vim->getAvailableResources().cpu;
		if (availableCPU > maxCpuAvailable)
		{
			bestHost = mecHost;
			maxCpuAvailable = availableCPU;
		}
	}
	return bestHost;

}

}
