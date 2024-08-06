#include "NodeLocation.h"
#include <math.h>

NodeLocation::NodeLocation(){
    this->x = 0;
    this->y = 0;
    this->z = 0;
}

NodeLocation::NodeLocation(long newX, long newY, long newZ){
    this->x = newX;
    this->y = newY;
    this->z = newZ;
}

NodeLocation::~NodeLocation(){
    // Do nothing
}

long NodeLocation::getX(){
    return this->x;
}

long NodeLocation::getY(){
    return this->y;
}

long NodeLocation::getZ(){
    return this->z;
}

void NodeLocation::setX(long newX){
    this->x = newX;
}

void NodeLocation::setY(long newY){
    this->y = newY;
}   

void NodeLocation::setZ(long newZ){
    this->z = newZ;
}