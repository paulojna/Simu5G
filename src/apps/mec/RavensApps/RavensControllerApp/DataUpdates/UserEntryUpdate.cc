#include "UserEntryUpdate.h"
#include <string>

namespace simu5g {

UserEntryUpdate::UserEntryUpdate(){
    this->address = "";
    this->currentMEHId = "";
    this->nextMEHId = "";
    this->timestamp = 0.0;
    this->accessPointId = "";
    this->ueId = "";
    this->x = 0;
    this->y = 0;
    this->hSpeed = 0;
    this->bearing = 0;
    this->distanceToAp = 0.0;
}

UserEntryUpdate::UserEntryUpdate(const std::string& address, const std::string& currentMEHId, const std::string& nextMEHId, omnetpp::simtime_t timestamp, const std::string& accessPointId, const std::string& ueId, long x, long y, long hSpeed, long bearing, double distanceToAp){
    this->address = address;
    this->currentMEHId = currentMEHId;
    this->nextMEHId = nextMEHId;
    this->timestamp = timestamp;
    this->accessPointId = accessPointId;
    this->ueId = ueId;
    this->x = x;
    this->y = y;
    this->hSpeed = hSpeed;
    this->bearing = bearing;
    this->distanceToAp = distanceToAp;
}

UserEntryUpdate::~UserEntryUpdate(){}

// setters
void UserEntryUpdate::setCurrentMEHId(const std::string& currentMEHId){
    this->currentMEHId = currentMEHId;
}

void UserEntryUpdate::setNextMEHId(const std::string& nextMEHId){
    this->nextMEHId = nextMEHId;
}

void UserEntryUpdate::setAddress(const std::string& address){
    this->address = address;
}

void UserEntryUpdate::setTimestamp(omnetpp::simtime_t timestamp){
    this->timestamp = timestamp;
}

void UserEntryUpdate::setAccessPointId(const std::string& accessPointId){
    this->accessPointId = accessPointId;
}

void UserEntryUpdate::setUeId(const std::string& ueId){
    this->ueId = ueId;
}

void UserEntryUpdate::setX(long x){
    this->x = x;
}

void UserEntryUpdate::setY(long y){
    this->y = y;
}

void UserEntryUpdate::setHSpeed(long hSpeed){
    this->hSpeed = hSpeed;
}

void UserEntryUpdate::setBearing(long bearing){
    this->bearing = bearing;
}

void UserEntryUpdate::setDistanceToAp(double distanceToAp){
    this->distanceToAp = distanceToAp;
}

//getters
const std::string& UserEntryUpdate::getCurrentMEHId() const{
    return currentMEHId;
}

const std::string& UserEntryUpdate::getNextMEHId() const{
    return nextMEHId;
}

const std::string& UserEntryUpdate::getAddress() const{
    return address;
}

omnetpp::simtime_t UserEntryUpdate::getTimestamp() const{
    return timestamp;
}

const std::string& UserEntryUpdate::getAccessPointId() const{
    return accessPointId;
}

const std::string& UserEntryUpdate::getUeId() const{
    return ueId;
}

long UserEntryUpdate::getX() const{
    return x;
}

long UserEntryUpdate::getY() const{
    return y;
}

long UserEntryUpdate::getHSpeed() const{
    return hSpeed;
}

long UserEntryUpdate::getBearing() const{
    return bearing;
}

double UserEntryUpdate::getDistanceToAp() const{
    return distanceToAp;
}

}