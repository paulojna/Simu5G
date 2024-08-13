#include "UserLocation.h"
#include <math.h>

namespace simu5g {

UserLocation::UserLocation(): NodeLocation(0, 0, 0){
    this->bearing = 0;
    this->horizontalSpeed = 0;
}

UserLocation::UserLocation(long newX, long newY, long newZ, long newBearing, long newHorizontalSpeed): NodeLocation(newX, newY, newZ){
    this->bearing = newBearing;
    this->horizontalSpeed = newHorizontalSpeed;
}

UserLocation::~UserLocation(){
    // Do nothing
}

long UserLocation::getBearing(){
    return this->bearing;
}

long UserLocation::getHorizontalSpeed(){
    return this->horizontalSpeed;
}

void UserLocation::setBearing(long newBearing){
    this->bearing = newBearing;
}

void UserLocation::setHorizontalSpeed(long newHorizontalSpeed){
    this->horizontalSpeed = newHorizontalSpeed;
}

}
