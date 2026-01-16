#include "UserData.h"
#include <math.h>

namespace simu5g {

UserData::UserData(){
    this->address = "";
    this->accessPointId = "";
    this->currentLocation = UserLocation();
    this->distance_to_ap = 0.0;

    this->dl_nongbr_delay_ue = -1.0;
    this->dl_nongbr_pdr_ue = -1.0;
    this->dl_nongbr_data_volume_ue = -1.0;
    this->ul_nongbr_delay_ue = -1.0;
    this->ul_nongbr_pdr_ue = -1.0;
    this->ul_nongbr_data_volume_ue = -1.0;

    this->lastUpdated = 0; // Initialize
	this->rnisUpdate = 0;
	this->lsUpdate = 0;

}

UserData::UserData(const std::string& address, AccessPointData& accessPointData, UserLocation& currentLocation){
    this->address = address;
    this->accessPointId = accessPointData.getAccessPointId();
    this->currentLocation = currentLocation;
    this->distance_to_ap = calculateDistanceToAP(accessPointData.getAccessPointLocation().getX(), accessPointData.getAccessPointLocation().getY(), currentLocation.getX(), currentLocation.getY());

    this->dl_nongbr_delay_ue = -1.0;
    this->dl_nongbr_pdr_ue = -1.0;
    this->dl_nongbr_data_volume_ue = -1.0;
    this->ul_nongbr_delay_ue = -1.0;
    this->ul_nongbr_pdr_ue = -1.0;
    this->ul_nongbr_data_volume_ue = -1.0;

    this->lastUpdated = 0; // Initialize with current time
	this->rnisUpdate = 0;
	this->lsUpdate = 0;

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

void UserData::setLastUpdated(omnetpp::simtime_t time){
    this->lastUpdated = time;
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

omnetpp::simtime_t UserData::getLastUpdated() const{
    return this->lastUpdated;
}

// RNIS Radio Metrics Getters
double UserData::getDlNongbrDelayUe() const {
    return this->dl_nongbr_delay_ue;
}

double UserData::getDlNongbrPdrUe() const {
    return this->dl_nongbr_pdr_ue;
}

double UserData::getDlNongbrDataVolumeUe() const {
    return this->dl_nongbr_data_volume_ue;
}

double UserData::getUlNongbrDelayUe() const {
    return this->ul_nongbr_delay_ue;
}

double UserData::getUlNongbrPdrUe() const {
    return this->ul_nongbr_pdr_ue;
}

double UserData::getUlNongbrDataVolumeUe() const {
    return this->ul_nongbr_data_volume_ue;
}

// RNIS Radio Metrics Setters
void UserData::setDlNongbrDelayUe(double delay) {
    this->dl_nongbr_delay_ue = delay;
}

void UserData::setDlNongbrPdrUe(double pdr) {
    this->dl_nongbr_pdr_ue = pdr;
}

void UserData::setDlNongbrDataVolumeUe(double dataVolume) {
    this->dl_nongbr_data_volume_ue = dataVolume;
}

void UserData::setUlNongbrDelayUe(double delay) {
    this->ul_nongbr_delay_ue = delay;
}

void UserData::setUlNongbrPdrUe(double pdr) {
    this->ul_nongbr_pdr_ue = pdr;
}

void UserData::setUlNongbrDataVolumeUe(double dataVolume) {
    this->ul_nongbr_data_volume_ue = dataVolume;
}

// method that calculates the eculedean distance between the user and a given x,y point
double UserData::calculateDistanceToAP(long x_AP, long y_AP, long x_UE, long y_UE){
    return sqrt(pow(x_AP - x_UE, 2) + pow(y_AP - y_UE, 2));
}

	omnetpp::simtime_t UserData::getRnisUpdate() const {
	return this->rnisUpdate;
}

	omnetpp::simtime_t UserData::getLsUpdate() const {
	return this->lsUpdate;
}

	void UserData::setRnisUpdate(omnetpp::simtime_t time) {
	this->rnisUpdate = time;
}

	void UserData::setLsUpdate(omnetpp::simtime_t time) {
	this->lsUpdate = time;
}

}