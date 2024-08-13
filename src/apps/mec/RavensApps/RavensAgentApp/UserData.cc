#include "UserData.h"
#include <math.h>

namespace simu5g {

UserData::UserData(){
    this->address = "";
    this->accessPointId = "";
    this->currentLocation = UserLocation();
    this->distance_to_ap = 0.0;
}

UserData::UserData(const std::string& address, AccessPointData& accessPointData, UserLocation& currentLocation){
    this->address = address;
    this->accessPointId = accessPointData.getAccessPointId();
    this->currentLocation = currentLocation;
    this->distance_to_ap = calculateDistanceToAP(accessPointData.getAccessPointLocation().getX(), accessPointData.getAccessPointLocation().getY(), currentLocation.getX(), currentLocation.getY());
}

UserData::~UserData(){}

// setters
void UserData::setAddress(const std::string& address){
    this->address = address;
}

void UserData::setAccessPointId(const std::string& accessPointId){
    this->accessPointId = accessPointId;
}   

void UserData::setCurrentLocation(const UserLocation& currentLocation){
    this->currentLocation = currentLocation;
}   

// getters
std::string UserData::getAddress() const{
    return this->address;
}

std::string UserData::getAccessPointId() const{
    return this->accessPointId;
}

UserLocation UserData::getCurrentLocation() const{
    return this->currentLocation;
}

double UserData::getDistanceToAP() const{
    return this->distance_to_ap;
}

// method that calculates the eculedean distance between the user and a given x,y point
double UserData::calculateDistanceToAP(long x_AP, long y_AP, long x_UE, long y_UE){
    return sqrt(pow(x_AP - x_UE, 2) + pow(y_AP - y_UE, 2));
}

}