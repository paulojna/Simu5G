#ifndef APPS_MEC_MECSERVICES_LOCATIONSERVICE_RESOURCES_USER_DENSITY_NOTIFICATION_SUBSCRIPTION_H_
#define APPS_MEC_MECSERVICES_LOCATIONSERVICE_RESOURCES_USER_DENSITY_NOTIFICATION_SUBSCRIPTION_H_

#include "nodes/mec/MECPlatform/MECServices/Resources/SubscriptionBase.h"
#include "inet/common/geometry/common/Coord.h"
#include "inet/common/INETDefs.h"
#include <set>
#include "nodes/mec/MECPlatform/MECServices/LocationService/resources/LocationApiDefs.h"
#include "nodes/mec/MECPlatform/MECServices/LocationService/resources/TerminalLocation.h"

namespace simu5g {

class LteBinder;

class UsersDensityNotificationSubscription: public SubscriptionBase
{
    public:
        UsersDensityNotificationSubscription();
        UsersDensityNotificationSubscription(unsigned int subId, inet::TcpSocket *socket , const std::string& baseResLocation,  std::set<omnetpp::cModule*>& eNodeBs);
        virtual ~UsersDensityNotificationSubscription();

        virtual void sendSubscriptionResponse() override;

        virtual bool fromJson(const nlohmann::ordered_json& json) override;
        virtual void sendNotification(EventNotification *event) override;
        virtual EventNotification* handleSubscription() override;

        virtual bool getCheckImmediate() const { return checkImmediate;}

        bool getFirstNotification() const {return firstNotificationSent;}
        omnetpp::simtime_t getLastoNotification() const { return lastNotification;}

        std::string getResourceUrl() const { return resourceURL;}
    
    protected:

        Binder* binder; //used to retrieve NodeId - Ipv4Address mapping
        omnetpp::simtime_t lastNotification;
        bool firstNotificationSent;

        //set of eNodeBs from which the user wants to receive notifications from
        std::set<MacNodeId> cells_;

        //map of cellId and number of users per BS
        std::map<MacCellId, int> density;
        
        //callbackReference
        std::string callbackData;// optional: YES
        std::string notifyURL; // optional: NO

        std::string resourceURL;
        bool checkImmediate; // optional: NO

        double frequency;


};

}

#endif // APPS_MEC_MECSERVICES_LOCATIONSERVICE_RESOURCES_USER_DENSITY_NOTIFICATION_SUBSCRIPTION_H_
