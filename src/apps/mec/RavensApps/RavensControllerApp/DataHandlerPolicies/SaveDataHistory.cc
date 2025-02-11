#include "SaveDataHistory.h"

namespace simu5g {

SaveDataHistory::SaveDataHistory(RavensControllerApp* controllerApp, std::string path):DataHandlerPolicyBase(controllerApp)
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
* we need to update the mehStateMap with the new users
* for the current strategy, we want to save the data in a csv file
*/
inet::Packet* SaveDataHistory::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
{
    // it might be a good idea to return a message anyway since we don't know what the future holds
    inet::Packet* pck = nullptr;

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
