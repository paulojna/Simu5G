#ifndef APPS_MEC_MECSERVICES_LOCATIONSERVICE_RESOURCES_USERS_LIST_NOTIFICATION_SUBSCRIPTION_H_
#define APPS_MEC_MECSERVICES_LOCATIONSERVICE_RESOURCES_USERS_LIST_NOTIFICATION_SUBSCRIPTION_H_

#include "nodes/mec/MECPlatform/MECServices/Resources/SubscriptionBase.h"
#include "inet/common/INETDefs.h"
#include "UserInfo.h"
#include <set>

namespace simu5g {

class LteBinder;

class UsersListNotificationSubscription: public SubscriptionBase
{
    public:
        UsersListNotificationSubscription();
        UsersListNotificationSubscription(unsigned int subId, inet::TcpSocket *socket , const std::string& baseResLocation,  std::set<omnetpp::cModule*, simu5g::utils::cModule_LessId>& eNodeBs);
        virtual ~UsersListNotificationSubscription();

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
    
            //set of UserInfo 
            std::list<UserInfo> userList;
            
            //callbackReference
            std::string callbackData;// optional: YES
            std::string notifyURL; // optional: NO
    
            std::string resourceURL;
            bool checkImmediate; // optional: NO
    
            double frequency;
};

}

#endif // APPS_MEC_MECSERVICES_LOCATIONSERVICE_RESOURCES_USERS_LIST_NOTIFICATION_SUBSCRIPTION_H_
