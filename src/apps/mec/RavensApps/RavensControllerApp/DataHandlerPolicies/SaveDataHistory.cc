#include "SaveDataHistory.h"

namespace simu5g {

SaveDataHistory::SaveDataHistory(RavensControllerApp* controllerApp, std::string path):DataHandlerPolicyBase(controllerApp)
{
    std::string name = path+std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber())+"_data_history.csv";
    csvFile.open(name, std::ios::out | std::ios::trunc);
    csvFile << "Timestamp,LastUpdated,MecHostId,AccessPointId,UEId,x,y,z,Speed,Bearing,DistanceToAccessPoint" << endl;
    csvFile.flush();
    EV << "SaveDataHistory::SaveDataHistory - file created in" << name << endl;
    std::cout << "SaveDataHistory::SaveDataHistory - file created in" << name << endl;
}

inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    // it might be a good idea to return a message anyway since we don't know what the future holds
    inet::Packet* pck = nullptr;

    auto usersInfoSnapshot = received_packet;

    // updates the users list of the host in the hostsData map
    std::map<std::string, MECHostData>::iterator it = controllerApp_->hostsData.find(usersInfoSnapshot->getMecHostId());
    it->second.setUsers(usersInfoSnapshot->getUsers());
    it->second.setLastUpdated(simTime());

    // run through all the hostsData map and save its state to a file
    for (auto it = controllerApp_->hostsData.begin(); it != controllerApp_->hostsData.end(); it++)
    {
        auto users = it->second.getUsers();
        if(users.size() == 0){
            // we want to send the information to the csv anyway but without the users
            csvFile << std::to_string(simTime().dbl()) << "," << std::to_string(it->second.getLastUpdated().dbl()) << "," << it->second.getHostId() << ",0,0,0,0,0,0,0,0" << endl;
            csvFile.flush();
            continue;
        }
        for (auto u_it = users.begin(); u_it != users.end(); u_it++)
        {
            // TODO: Binder has getMacNodeId(ipv4) and getOmnetId(macNodeId) so we can get the omnet id of the users
            
            
            csvFile << std::to_string(simTime().dbl()) << "," << std::to_string(it->second.getLastUpdated().dbl()) << "," << it->second.getHostId() << "," << u_it->second.getAccessPointId() << "," << u_it->first << "," << std::to_string(u_it->second.getCurrentLocation().getX()) << "," << std::to_string(u_it->second.getCurrentLocation().getY()) << "," << std::to_string(u_it->second.getCurrentLocation().getZ()) << "," << std::to_string(u_it->second.getCurrentLocation().getHorizontalSpeed()) << "," << std::to_string(u_it->second.getCurrentLocation().getBearing()) << "," << std::to_string(u_it->second.getDistanceToAP()) << endl;
            csvFile.flush();
        }
    }
    return pck;
}

SaveDataHistory::~SaveDataHistory()
{
    csvFile.close();
}

}
