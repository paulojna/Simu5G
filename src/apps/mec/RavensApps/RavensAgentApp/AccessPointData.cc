#include "AccessPointData.h"

namespace simu5g {

AccessPointData::AccessPointData(){
    this->accessPointId = "";
    this->accessPointLocation = NodeLocation();
}

AccessPointData::AccessPointData(const std::string& accessPointId, const NodeLocation& accessPointLocation){
    this->accessPointId = accessPointId;
    this->accessPointLocation = accessPointLocation;
}

// setters
void AccessPointData::setAccessPointId(const std::string& accessPointId){
    this->accessPointId = accessPointId;
}

void AccessPointData::setAccessPointLocation(const NodeLocation& accessPointLocation){
    this->accessPointLocation = accessPointLocation;
}

// getters
std::string AccessPointData::getAccessPointId() const{
    return this->accessPointId;
}

NodeLocation AccessPointData::getAccessPointLocation() const{
    return this->accessPointLocation;
}

}