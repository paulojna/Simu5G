#ifndef _USERLOCATION_H_
#define _USERLOCATION_H_

#include "NodeLocation.h"

namespace simu5g {

class UserLocation: public NodeLocation {
    private:
        long bearing;
        long horizontalSpeed;

    public:
        // Default constructor
        UserLocation();

        // Parameterized constructor
        UserLocation(long newX, long newY, long newZ, long newBearing, long newHorizontalSpeed);

        // Destructor
        virtual ~UserLocation();

        long getBearing();
        long getHorizontalSpeed();

        void setBearing(long newBearing);
        void setHorizontalSpeed(long newHorizontalSpeed);
};

}

#endif /* _USERLOCATION_H_ */
