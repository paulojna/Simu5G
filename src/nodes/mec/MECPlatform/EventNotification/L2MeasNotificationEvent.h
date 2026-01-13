//
// Created by Paulo Araújo
//

#ifndef NODES_MEC_MECPLATFORM_EVENTNOTIFICATION_L2MEASNOTIFICATIONEVENT_H_
#define NODES_MEC_MECPLATFORM_EVENTNOTIFICATION_L2MEASNOTIFICATIONEVENT_H_

  #include "nodes/mec/MECPlatform/EventNotification/EventNotification.h"
  #include "common/LteCommon.h"
  #include <set>

  namespace simu5g {

      /**
       * L2MeasNotificationEvent
       *
       * Event triggered when L2 measurement subscription needs to send a notification.
       * Contains the cells being monitored.
       */
      class L2MeasNotificationEvent : public EventNotification
      {
      protected:
          std::set<MacCellId> cells_;  // Cells being monitored

      public:
          L2MeasNotificationEvent(
              const std::string& subType,
              unsigned int subId,
              const std::set<MacCellId>& cells
          );

          virtual ~L2MeasNotificationEvent();

          // Getter
          const std::set<MacCellId>& getCells() const { return cells_; }
      };

  } // namespace simu5g

#endif
