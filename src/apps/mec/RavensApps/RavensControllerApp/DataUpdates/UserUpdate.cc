#include "UserUpdate.h"

namespace simu5g {

UserUpdate::UserUpdate(const std::string& add): address(add){}

UserUpdate::~UserUpdate(){}

// getters
const std::string& UserUpdate::getAddress(){
    return this->address;
}

}