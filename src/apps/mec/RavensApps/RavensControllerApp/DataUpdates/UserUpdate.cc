#include "UserUpdate.h"

UserUpdate::UserUpdate(const std::string& add): address(add){}

UserUpdate::~UserUpdate(){}

// getters
const std::string& UserUpdate::getAddress(){
    return this->address;
}
