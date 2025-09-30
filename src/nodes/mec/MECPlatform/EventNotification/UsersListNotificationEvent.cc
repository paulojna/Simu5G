#include "UsersListNotificationEvent.h"

namespace simu5g {

UsersListNotificationEvent::UsersListNotificationEvent() {}

UsersListNotificationEvent::UsersListNotificationEvent(const std::string& type, const int& subId, const std::vector<UserInfo>& usersList)
        : EventNotification(type, subId), usersList_(usersList){}

const std::vector<UserInfo>& UsersListNotificationEvent::getUsersList() const
{
    return usersList_;
}

UsersListNotificationEvent::~UsersListNotificationEvent() {}

}