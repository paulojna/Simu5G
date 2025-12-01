#ifndef _MECHOSTDATA_H_
#define _MECHOSTDATA_H_

#include <omnetpp.h>
#include <string>
#include <vector>
#include <map>
#include "apps/mec/RavensApps/RavensAgentApp/AccessPointData.h"
#include "apps/mec/RavensApps/RavensAgentApp/UserData.h"
#include "apps/mec/RavensApps/RavensAgentApp/AccessPointRadioInfoData.h" // Added include
#include "inet/networklayer/common/L3Address.h"

/*
    Simple structure to hold the data of a host, including connected Access Points and other relevant information
*/

namespace simu5g {

class MECHostData {
    private:
        std::string hostId;
        inet::L3Address remoteAddress;
        int port;
        std::vector<AccessPointData> accessPoints;
        AccessPointRadioInfoData apRadioInfo; // Added member
        // std::unordered_map<std::string, UserData> users;

    public:
        // Default constructor
        MECHostData();
        // Parameterized constructor
        MECHostData(const std::string& hostId, const std::vector<AccessPointData>& accessPoints, const std::unordered_map<std::string, UserData>& users);

        // Destructor
        ~MECHostData() {}

        std::string getHostId() const;
        inet::L3Address getL3Address() const;
        int getPort() const;
        std::vector<AccessPointData> getAccessPoints() const;
        AccessPointRadioInfoData getApRadioInfo() const; // Added getter
        
        //std::unordered_map<std::string, UserData> getUsers() const;
        //omnetpp::simtime_t getOriginTimestamp() const;

        std::vector<AccessPointData>& getAccessPoints();
        // std::unordered_map<std::string, UserData>& getUsers();

        void setHostId(const std::string& hostId);
        void setL3Address(inet::L3Address remoteAddress);
        void setPort(int port);
        void setAccessPoints(const std::vector<AccessPointData>& accessPoints);
        void setApRadioInfo(const AccessPointRadioInfoData& apRadioInfo); // Added setter
        
        // add one setUsers method that is not const
        // void setUsers(std::unordered_map<std::string, UserData>& users);
        // void setUsers(const std::unordered_map<std::string, UserData>& users);
        // void setOriginTimestamp(omnetpp::simtime_t time);
};

}

#endif /* _MECHOSTDATA_H_ */