#include "SaveDataHistory.h"

namespace simu5g {

SaveDataHistory::SaveDataHistory(RavensControllerApp* controllerApp, std::string path):LocationDataHandlerPolicyBase(controllerApp)
{
    std::string name = path+"run_"+std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber())+"_data_history.csv"; 
    csvFile.open(name, std::ios::out | std::ios::trunc);
    csvFile << "Timestamp,UEId,MEHId,AccessPointId,x,y,z,Speed,Bearing,DistanceToAccessPoint" << endl;
    csvFile.flush();
    EV << "SaveDataHistory::SaveDataHistory - file created in" << name << endl;
}

/*
* handleDataMessage is called when a RavensLinkUsersInfoSnapshotMessage is received
* this message contains the information about the users connected to the access point
* 1) We need to update the mehStateMap with the new users (this means remove inactive users 
*    and add/update users to the userStateMap)
* 2) for the current strategy, we also want to save the data in a csv file
*/
inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    // it might be a good idea to return a message anyway since we don't know what the future holds
    inet::Packet* pck = nullptr;

    // remove inactive users
    std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();

    // update the userStateMap
    controllerApp_->updateUserStateMap(received_packet);

    // print the userStateMap in the csv file
    for(auto user : controllerApp_->userStateMap){
        csvFile << user.second.timestamp << "," << user.first << "," << user.second.currentMEH << "," << user.second.userData.getAccessPointId() << "," << user.second.userData.getCurrentLocation().getX() << "," << user.second.userData.getCurrentLocation().getY() << "," << user.second.userData.getCurrentLocation().getZ() << "," << user.second.userData.getCurrentLocation().getHorizontalSpeed() << "," << user.second.userData.getCurrentLocation().getBearing() << "," << user.second.userData.getDistanceToAP() << endl;
    }
    csvFile.flush();
    return pck;
}

SaveDataHistory::~SaveDataHistory()
{
    csvFile.close();
}

}
