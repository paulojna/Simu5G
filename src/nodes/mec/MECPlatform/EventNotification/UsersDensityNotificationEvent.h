#ifndef NODES_MEC_MEPLATFORM_EVENTNOTIFICATION_USERSDENSITYNOTIFICATIONEVENT_H_
#define NODES_MEC_MEPLATFORM_EVENTNOTIFICATION_USERSDENSITYNOTIFICATIONEVENT_H_

#include "EventNotification.h"
#include <map>
#include "common/LteCommon.h"

namespace simu5g {

using namespace omnetpp;

class UsersDensityNotificationEvent: public EventNotification {
    public:
        UsersDensityNotificationEvent();
        UsersDensityNotificationEvent(const std::string& type, const int& subId, const std::map<MacCellId, int>& density);

        const std::map<MacCellId, int>& getDensity() const;

        virtual ~UsersDensityNotificationEvent();

    private:
        std::map<MacCellId, int> density_;
};

}

#endif /* NODES_MEC_MEPLATFORM_EVENTNOTIFICATION_USERSDENSITYNOTIFICATIONEVENT_H_ */

