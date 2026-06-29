#ifndef RAVENS_CONTROLLER_APP_LOCATIONDATAHANDLERPOLICYBASE_H_
#define RAVENS_CONTROLLER_APP_LOCATIONDATAHANDLERPOLICYBASE_H_

#include "../RavensControllerApp.h"

namespace simu5g {

using namespace omnetpp;

class RavensControllerApp;

// abstract class
class LocationDataHandlerPolicyBase
{
    friend class RavensControllerApp;
    
    protected:
        RavensControllerApp* controllerApp_;
        // = 0 indicates pure virtual function with no implementation in the base class
        virtual inet::Packet* handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) = 0;
        virtual void handleEventMessage(const RavensEventList& events, const std::string& sourceMEH) {}

    public:
        LocationDataHandlerPolicyBase(RavensControllerApp* controllerApp) { controllerApp_ = controllerApp; }
        virtual ~LocationDataHandlerPolicyBase() {}
};

}

#endif /* RAVENS_CONTROLLER_APP_LOCATIONDATAHANDLERPOLICYBASE_H_ */
