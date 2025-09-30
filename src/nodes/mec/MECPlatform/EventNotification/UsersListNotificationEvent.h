#ifndef NODES_MEC_MEPLATFORM_EVENTNOTIFICATION_USERSLISTNOTIFICATIONEVENT_H_
#define NODES_MEC_MEPLATFORM_EVENTNOTIFICATION_USERSLISTNOTIFICATIONEVENT_H_

#include "EventNotification.h"
#include "common/LteCommon.h"
#include "nodes/mec/MECPlatform/MECServices/LocationService/resources/UserInfo.h"

namespace simu5g {

using namespace omnetpp;

class UsersListNotificationEvent: public EventNotification {
    public:
        UsersListNotificationEvent();
        UsersListNotificationEvent(const std::string& type, const int& subId, const std::vector<UserInfo>& usersList);

        const std::vector<UserInfo>& getUsersList() const;

        virtual ~UsersListNotificationEvent();

    private:
        std::vector<UserInfo> usersList_;
};

}

#endif /* NODES_MEC_MEPLATFORM_EVENTNOTIFICATION_USERSLISTNOTIFICATIONEVENT_H_ */