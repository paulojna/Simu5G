#include "UsersListNotificationEvent.h"

namespace simu5g {

UsersListNotificationEvent::UsersListNotificationEvent() {}

UsersListNotificationEvent::UsersListNotificationEvent(const std::string& type, const int& subId, const std::list<UserInfo>& usersList)
        : EventNotification(type, subId), usersList_(usersList){}

const std::list<UserInfo>& UsersListNotificationEvent::getUsersList() const
{
    return usersList_;
}

UsersListNotificationEvent::~UsersListNotificationEvent() {}

}