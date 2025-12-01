#ifndef _USERDATA_H_
#define _USERDATA_H_

#include <string>
#include "UserLocation.h"
#include "AccessPointData.h"

namespace simu5g {

class UserData
{
    protected:
        std::string address;
        std::string accessPointId;
        UserLocation currentLocation;
        double distance_to_ap;

        // RNIS Radio Metrics
        double dl_nongbr_delay_ue;
        double dl_nongbr_throughput_ue;
        double ul_nongbr_throughput_ue;
        double dl_nongbr_pdr_ue;

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

        // RNIS Radio Metrics Getters
        double getDlNongbrDelayUe() const;
        double getDlNongbrThroughputUe() const;
        double getUlNongbrThroughputUe() const;
        double getDlNongbrPdrUe() const;

        // RNIS Radio Metrics Setters
        void setDlNongbrDelayUe(double delay);
        void setDlNongbrThroughputUe(double throughput);
        void setUlNongbrThroughputUe(double throughput);
        void setDlNongbrPdrUe(double pdr);

        // method that calculates the eculedean distance between two points
        double calculateDistanceToAP(long x_AP, long y_AP, long x_UE, long y_UE);

        bool operator==(const UserData& other) const {
        return address == other.address && currentLocation == other.currentLocation;
    }
};  

}

#endif /* _USERDATA_H_ */

