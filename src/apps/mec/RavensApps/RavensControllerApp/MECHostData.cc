#include "MECHostData.h"

using namespace omnetpp;


MECHostData::MECHostData(){
    this->hostId = "";
    this->lastUpdated = omnetpp::simTime();
    this->accessPoints = std::vector<AccessPointData>();
    this->users = std::map<std::string, UserData>();
}

MECHostData::MECHostData(const std::string& hostId, const std::vector<AccessPointData>& accessPoints, const std::map<std::string, UserData>& users){
    this->hostId = hostId;
    this->accessPoints = accessPoints;
    this->users = users;
}

// setters
void MECHostData::setHostId(const std::string& hostId){
    this->hostId = hostId;
    // update lastUpdated with the current timestamp
    setLastUpdated(omnetpp::simTime());
}

void MECHostData::setAccessPoints(const std::vector<AccessPointData>& accessPoints){
    this->accessPoints = accessPoints;
    setLastUpdated(omnetpp::simTime());
}   

void MECHostData::setUsers(const std::map<std::string, UserData>& users){
    this->users = users;
    setLastUpdated(omnetpp::simTime());
}   

void MECHostData::setLastUpdated(omnetpp::simtime_t lastUpdated){
    this->lastUpdated = lastUpdated;
}

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

std::map<std::string, UserData> MECHostData::getUsers() const{
    return this->users;
}   

omnetpp::simtime_t MECHostData::getLastUpdated() const{
    return this->lastUpdated;
}

inet::L3Address MECHostData::getL3Address() const{
    return this->remoteAddress;
}

int MECHostData::getPort() const{
    return this->port;
}


