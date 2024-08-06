#ifndef RAVENS_CONTROLLER_APP_DATAHANDLERPOLICYBASE_H_
#define RAVENS_CONTROLLER_APP_DATAHANDLERPOLICYBASE_H_

#include "../RavensControllerApp.h"

using namespace omnetpp;

class RavensControllerApp;

// abstract class
class DataHandlerPolicyBase
{
    friend class RavensControllerApp;
    
    protected:
        RavensControllerApp* controllerApp_;
        // = 0 indicates pure virtual function with no implementation in the base class
        virtual inet::Packet* handleDataMessage(inet::Ptr<const UsersInfoSnapshotMessage> received_packet) = 0;

    public:
        DataHandlerPolicyBase(RavensControllerApp* controllerApp) { controllerApp_ = controllerApp; }
        virtual ~DataHandlerPolicyBase() {}
};

#endif /* RAVENS_CONTROLLER_APP_DATAHANDLERPOLICYBASE_H_ */
