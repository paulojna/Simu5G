#ifndef _MECHOSTDATA_H_
#define _MECHOSTDATA_H_

#include <omnetpp.h>
#include <string>
#include <vector>
#include <map>
#include "apps/mec/RavensApps/RavensAgentApp/AccessPointData.h"
#include "apps/mec/RavensApps/RavensAgentApp/UserData.h"
#include "inet/networklayer/common/L3Address.h"

/*
    Simple structure to hold the data of a host, including Access Points and Users and the last time it was updated
*/

class MECHostData {
    private:
        std::string hostId;
        inet::L3Address remoteAddress;
        int port;
        omnetpp::simtime_t lastUpdated;
        std::vector<AccessPointData> accessPoints;
        std::map<std::string, UserData> users;

    public:
        // Default constructor
        MECHostData();
        // Parameterized constructor
        MECHostData(const std::string& hostId, const std::vector<AccessPointData>& accessPoints, const std::map<std::string, UserData>& users);

        // Destructor
        ~MECHostData() {}

        std::string getHostId() const;
        inet::L3Address getL3Address() const;
        int getPort() const;
        std::vector<AccessPointData> getAccessPoints() const;
        std::map<std::string, UserData> getUsers() const;
        omnetpp::simtime_t getLastUpdated() const;

        void setHostId(const std::string& hostId);
        void setL3Address(inet::L3Address remoteAddress);
        void setPort(int port);
        void setAccessPoints(const std::vector<AccessPointData>& accessPoints);
        void setUsers(const std::map<std::string, UserData>& users);
        void setLastUpdated(omnetpp::simtime_t lastUpdated);
};

#endif /* _MECHOSTDATA_H_ */