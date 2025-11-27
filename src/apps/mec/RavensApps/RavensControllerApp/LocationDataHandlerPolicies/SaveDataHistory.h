#ifndef RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_
#define RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_

#include "LocationDataHandlerPolicyBase.h"

#include <fstream>
#include <iostream>
#include <string>

namespace simu5g {

using namespace omnetpp;

//class RavensControllerApp;

class SaveDataHistory : public LocationDataHandlerPolicyBase
{
    protected:
        std::ofstream csvFile;
        std::ofstream lifecycleFile;
        int msgCount_;
        static const int FLUSH_INTERVAL_ = 100; // Flush every 100 messages
        virtual inet::Packet* handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet) override;
    public:
        SaveDataHistory(RavensControllerApp* controllerApp, std::string path);
        virtual ~SaveDataHistory();
};

}

#endif /* "RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_" */
