#ifndef _USERDATA_H_
#define _USERDATA_H_

#include <string>
#include "UserLocation.h"
#include "AccessPointData.h"

class UserData
{
    protected:
        std::string address;
        std::string accessPointId;
        UserLocation currentLocation;
        double distance_to_ap;

    public:
        UserData();
        UserData(const std::string& address, AccessPointData& accessPointData, UserLocation& currentLocation);
        virtual ~UserData();

        // setters
        void setAddress(const std::string& address);
        void setAccessPointId(const std::string& accessPointId);
        void setCurrentLocation(const UserLocation& currentLocation);

        // getters
        std::string getAddress() const;
        std::string getAccessPointId() const;
        UserLocation getCurrentLocation() const;
        double getDistanceToAP() const;

        // method that calculates the eculedean distance between two points
        double calculateDistanceToAP(long x_AP, long y_AP, long x_UE, long y_UE);

         
};  

#endif /* _USERDATA_H_ */

