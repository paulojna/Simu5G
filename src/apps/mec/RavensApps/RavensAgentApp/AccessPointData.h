#ifndef _ACCESSPOINTDATA_H_
#define _ACCESSPOINTDATA_H_

#include "NodeLocation.h"
#include <string>

namespace simu5g {

class AccessPointData {
    private:
        std::string accessPointId;
        NodeLocation accessPointLocation;

    public:
        // Default constructor
        AccessPointData();

        // Parameterized constructor
        AccessPointData(const std::string& accessPointId, const NodeLocation& accessPointLocation);

        // Destructor
        ~AccessPointData() {}

        std::string getAccessPointId() const;
        NodeLocation getAccessPointLocation() const;

        void setAccessPointId(const std::string& accessPointId);
        void setAccessPointLocation(const NodeLocation& accessPointLocation);
};

}

#endif /* _ACCESSPOINTDATA_H_ */