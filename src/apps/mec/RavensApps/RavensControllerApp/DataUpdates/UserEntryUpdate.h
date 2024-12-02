#ifndef USERENTRYUPDATE_H
#define USERENTRYUPDATE_H
#include <string>
#include "omnetpp.h"

namespace simu5g {

class UserEntryUpdate 
{
    protected:
        std::string address;
        std::string currentMEHId;
        std::string nextMEHId;
        omnetpp::simtime_t timestamp;
        std::string accessPointId;
        std::string ueId;
        long x;
        long y;
        long hSpeed;
        long bearing;
        double distanceToAp;


    public:
        UserEntryUpdate();
        UserEntryUpdate(const std::string& address, const std::string& currentMEHId, const std::string& nextMEHId, omnetpp::simtime_t timestamp, const std::string& accessPointId, const std::string& ueId, long x, long y, long hSpeed, long bearing, double distanceToAp);
        virtual ~UserEntryUpdate();

        // setters
        void setCurrentMEHId(const std::string& currentMEHId);
        void setNextMEHId(const std::string& nextMEHId);
        void setAddress(const std::string& address);
        void setTimestamp(omnetpp::simtime_t timestamp);
        void setAccessPointId(const std::string& accessPointId);
        void setUeId(const std::string& ueId);
        void setX(long x);
        void setY(long y);
        void setHSpeed(long hSpeed);
        void setBearing(long bearing);
        void setDistanceToAp(double distanceToAp);

        // getters
        const std::string& getCurrentMEHId() const;
        const std::string& getNextMEHId() const;
        const std::string& getAddress() const;
        omnetpp::simtime_t getTimestamp() const;
        const std::string& getAccessPointId() const;
        const std::string& getUeId() const;
        long getX() const;
        long getY() const;
        long getHSpeed() const;
        long getBearing() const;
        double getDistanceToAp() const;
};

}

#endif