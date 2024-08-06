#include "UsersDensityNotificationEvent.h"

namespace simu5g {

UsersDensityNotificationEvent::UsersDensityNotificationEvent() {}

UsersDensityNotificationEvent::UsersDensityNotificationEvent(const std::string& type, const int& subId, const std::map<MacCellId, int>& density)
        : EventNotification(type, subId), density_(density){}

const std::map<MacCellId, int>& UsersDensityNotificationEvent::getDensity() const
{
    return density_;
}

UsersDensityNotificationEvent::~UsersDensityNotificationEvent() {}

}