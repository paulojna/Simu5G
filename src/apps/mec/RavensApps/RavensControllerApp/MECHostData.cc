#include "MECHostData.h"


namespace simu5g {

using namespace omnetpp;


MECHostData::MECHostData(){
    this->hostId = "";
    this->accessPoints = std::vector<AccessPointData>();
    // this->users = std::unordered_map<std::string, UserData>();
}

MECHostData::MECHostData(const std::string& hostId, const std::vector<AccessPointData>& accessPoints, const std::unordered_map<std::string, UserData>& users){
    this->hostId = hostId;
    this->accessPoints = accessPoints;
    // this->users = users;
}

// setters
void MECHostData::setHostId(const std::string& hostId){
    this->hostId = hostId;
}

void MECHostData::setAccessPoints(const std::vector<AccessPointData>& accessPoints){
    this->accessPoints = accessPoints;
}   

// void MECHostData::setUsers(const std::unordered_map<std::string, UserData>& users){
//     this->users = users;
// }   

void MECHostData::setL3Address(inet::L3Address remoteAddress){
    this->remoteAddress = remoteAddress;
}

void MECHostData::setPort(int port){
    this->port = port;
}

// getters
std::string MECHostData::getHostId() const{
    return this->hostId;
}

std::vector<AccessPointData> MECHostData::getAccessPoints() const{
    return this->accessPoints;
}   

// std::unordered_map<std::string, UserData> MECHostData::getUsers() const{
//     return this->users;
// }   

// std::unordered_map<std::string, UserData>& MECHostData::getUsers() {
//     return users;
// }

inet::L3Address MECHostData::getL3Address() const{
    return this->remoteAddress;
}

int MECHostData::getPort() const{
    return this->port;
}

}
