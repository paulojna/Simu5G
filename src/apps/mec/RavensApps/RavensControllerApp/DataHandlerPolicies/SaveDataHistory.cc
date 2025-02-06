#include "SaveDataHistory.h"

namespace simu5g {

SaveDataHistory::SaveDataHistory(RavensControllerApp* controllerApp, std::string path):DataHandlerPolicyBase(controllerApp)
{
    std::string name = path+"run_"+std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber())+"_data_history.csv"; 
    csvFile.open(name, std::ios::out | std::ios::trunc);
    csvFile << "Timestamp,LastUpdated,MecHostId,AccessPointId,UEId,x,y,z,Speed,Bearing,DistanceToAccessPoint" << endl;
    csvFile.flush();
    EV << "SaveDataHistory::SaveDataHistory - file created in" << name << endl;
}

/*
* handleDataMessage is called when a RavensLinkUsersInfoSnapshotMessage is received
* this message contains the information about the users connected to the access point
* we need to update the mehStateMap with the new users
* for the current strategy, we want to save the data in a csv file
*/
inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    // it might be a good idea to return a message anyway since we don't know what the future holds
    inet::Packet* pck = nullptr;

    auto usersInfoSnapshot = received_packet;

    std::string packet_mecHostId = usersInfoSnapshot->getMecHostId();
    simtime_t packet_originTimestamp = usersInfoSnapshot->getTimeStamp();

    /*
    * first we need to update the userStateMap with the new users  
    * if a change of currentMEH is detected, we need to update the mehStateMap
    */
    for(auto user : usersInfoSnapshot->getUsers()){
        // check if the user is already in the userStateMap
        auto userIt = controllerApp_->userStateMap.find(user.first);
        if(userIt == controllerApp_->userStateMap.end()){
            // the user is not in the userStateMap, we need to add it
            controllerApp_->userStateMap[user.first].userId = user.second.getAddress();
            controllerApp_->userStateMap[user.first].currentMEH = packet_mecHostId;
            controllerApp_->userStateMap[user.first].lastUpdate_origin = packet_originTimestamp;
            controllerApp_->userStateMap[user.first].lastUpdate = simTime();
            std::cout << "User " << user.first << " added to userStateMap" << endl;
        }
        else{
            // the user is already in the userStateMap, we need to update the lastUpdate_origin and mecHostId if it has changed
            if(userIt->second.currentMEH != packet_mecHostId){
                //std::cout << "User " << user.first << " changed its currentMEH from " << userIt->second.currentMEH << " to " << packet_mecHostId << endl;

                // because the user has changed its currentMEH, we need to update the mehStateMap
                auto mehIt = controllerApp_->mehStateMap.find(userIt->second.currentMEH);
                std::cout << "User " << user.first << " changed its currentMEH to " << mehIt->second.mecHostId << endl;
                if(mehIt == controllerApp_->mehStateMap.end()){
                    EV << "SaveDataHistory::handleDataMessage - mecHostId not found in mehStateMap" << endl;
                    return pck;
                }
                // we need to remove the user from the previous meh
                auto& users = mehIt->second.hostData.getUsers();
                // lets print the users in the previous meh
                std::cout << "Users in the previous meh before removal: " << endl;
                for(auto u : users){
                    std::cout << u.first << " " << u.second.getAddress() << endl;
                }
                users.erase(user.first);
                // lets print the users again
                std::cout << "Users in the previous meh after removal: " << endl;
                for(auto u : users){
                    std::cout << u.first << " " << u.second.getAddress() << endl;
                }

                userIt->second.currentMEH = packet_mecHostId;
                userIt->second.lastUpdate_origin = packet_originTimestamp;
                userIt->second.lastUpdate = simTime();
            }
        }
    }

    /* 
    * with the information in the userInfoSnapshot, we can update the mehStateMap
    * for that we need to find the hostId in the mehStateMap
    */
    std::string mecHostId = usersInfoSnapshot->getMecHostId();
    auto it = controllerApp_->mehStateMap.find(mecHostId);
    if(it == controllerApp_->mehStateMap.end()){
        EV << "SaveDataHistory::handleDataMessage - mecHostId not found in mehStateMap" << endl;
        return pck;
    }
    
    /* 
    * update the users of the host only if originTimestamp in hostData is earlier that the timeStamp in usersInfoSnapshot, 
    * this is to avoid updating the users of the host with old information
    */
    if(it->second.hostData.getOriginTimestamp() < usersInfoSnapshot->getTimeStamp()){
        it->second.hostData.setUsers(usersInfoSnapshot->getUsers());
        it->second.hostData.setOriginTimestamp(usersInfoSnapshot->getTimeStamp());
        it->second.lastUpdate = simTime();
    }

    /*
    * run through the information on each hostData at mehStateMap and save the data in the previsouly created csv file
    */
    for (auto it = controllerApp_->mehStateMap.begin(); it != controllerApp_->mehStateMap.end(); it++)
    {
        auto users = it->second.hostData.getUsers();
        if(users.empty()){
            // we want to send the information to the csv anyway but without the users
            csvFile << std::to_string(simTime().dbl()) << "," << std::to_string(it->second.lastUpdate.dbl()) << "," << it->second.mecHostId << ",0,0,0,0,0,0,0,0" << endl;
            csvFile.flush();
            continue;
        }
        for (auto u_it = users.begin(); u_it != users.end(); u_it++)
        {
            csvFile << std::to_string(simTime().dbl()) << "," << std::to_string(it->second.lastUpdate.dbl()) << "," << it->second.mecHostId << "," << u_it->second.getAccessPointId() << "," << u_it->first << "," << std::to_string(u_it->second.getCurrentLocation().getX()) << "," << std::to_string(u_it->second.getCurrentLocation().getY()) << "," << std::to_string(u_it->second.getCurrentLocation().getZ()) << "," << std::to_string(u_it->second.getCurrentLocation().getHorizontalSpeed()) << "," << std::to_string(u_it->second.getCurrentLocation().getBearing()) << "," << std::to_string(u_it->second.getDistanceToAP()) << endl;
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
