//
// Created by Paulo Araújo
//

#include "nodes/mec/MECPlatform/EventNotification/L2MeasNotificationEvent.h"

  namespace simu5g {

      L2MeasNotificationEvent::L2MeasNotificationEvent(
          const std::string& subType,
          unsigned int subId,
          const std::set<MacCellId>& cells)
          : EventNotification(subType, subId)
      {
          cells_ = cells;
      }

      L2MeasNotificationEvent::~L2MeasNotificationEvent(){}

  } // namespace simu5g
