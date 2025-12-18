#ifndef _USERDATA_H_
#define _USERDATA_H_

#include <string>
#include "omnetpp.h" // Added for simtime_t
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
        
        omnetpp::simtime_t lastUpdated; // Added timestamp

        // RNIS Radio Metrics
        double dl_nongbr_delay_ue;
        double dl_nongbr_pdr_ue;
        double dl_nongbr_data_volume_ue;
        double ul_nongbr_delay_ue;
        double ul_nongbr_pdr_ue;
        double ul_nongbr_data_volume_ue;

    public:
        UserData();
        UserData(const std::string& address, AccessPointData& accessPointData, UserLocation& currentLocation);
        virtual ~UserData();

        // setters
        void setAddress(const std::string& address);
        void setAccessPointId(const std::string& accessPointId);
        void setCurrentLocation(const UserLocation& currentLocation);
        void setLastUpdated(omnetpp::simtime_t time); // Added setter

        // getters
        std::string getAddress() const;
        std::string getAccessPointId() const;
        UserLocation getCurrentLocation() const;
        double getDistanceToAP() const;
        omnetpp::simtime_t getLastUpdated() const; // Added getter

        // RNIS Radio Metrics Getters
        double getDlNongbrDelayUe() const;
        double getDlNongbrPdrUe() const;
        double getDlNongbrDataVolumeUe() const;
        double getUlNongbrDelayUe() const;
        double getUlNongbrPdrUe() const;
        double getUlNongbrDataVolumeUe() const;

        // RNIS Radio Metrics Setters
        void setDlNongbrDelayUe(double delay);
        void setDlNongbrPdrUe(double pdr);
        void setDlNongbrDataVolumeUe(double dataVolume);
        void setUlNongbrDelayUe(double delay);
        void setUlNongbrPdrUe(double pdr);
        void setUlNongbrDataVolumeUe(double dataVolume);

        // method that calculates the eculedean distance between two points
        double calculateDistanceToAP(long x_AP, long y_AP, long x_UE, long y_UE);

        bool operator==(const UserData& other) const {
        return address == other.address && currentLocation == other.currentLocation;
    }
};  

}

#endif /* _USERDATA_H_ */

