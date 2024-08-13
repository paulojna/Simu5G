#include "UserMEHUpdate.h"

namespace simu5g {

UserMEHUpdate::UserMEHUpdate(){
    this->address = "";
    this->lastMEHId = "";
    this->newMEHId = "";
}

UserMEHUpdate::UserMEHUpdate(const std::string& add, const std::string& lastMEHId, const std::string& newMEHId){
    this->address = add;
    this->lastMEHId = lastMEHId;
    this->newMEHId = newMEHId;
}   

UserMEHUpdate::~UserMEHUpdate(){}

// setters
void UserMEHUpdate::setLastMEHId(const std::string& lastMEHId){
    this->lastMEHId = lastMEHId;
}

void UserMEHUpdate::setNewMEHId(const std::string& newMEHId){
    this->newMEHId = newMEHId;
}

void UserMEHUpdate::setAddress(const std::string& address){
    this->address = address;
}

// getters
const std::string& UserMEHUpdate::getLastMEHId() const{
    return lastMEHId;
}

const std::string& UserMEHUpdate::getNewMEHId() const{
    return newMEHId;
}

const std::string& UserMEHUpdate::getAddress() const{
    return address;
}

}