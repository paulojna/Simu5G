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
        std::ofstream userFile;
        std::ofstream lifecycleFile;
        std::ofstream radioStatsFile;
        int msgCount_;
        static const int FLUSH_INTERVAL_ = 100;
        virtual inet::Packet* handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) override;
        virtual void handleEventMessage(inet::Ptr<const RavensLinkEventMessage> event) override;
    public:
        SaveDataHistory(RavensControllerApp* controllerApp, std::string path);
        void addUserUpdate(UserMEHUpdate &update);
        virtual ~SaveDataHistory();
};

}

#endif /* "RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_" */
