#ifndef RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_
#define RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_

#include "DataHandlerPolicyBase.h"

#include <fstream>
#include <iostream>
#include <string>

namespace simu5g {

using namespace omnetpp;

//class RavensControllerApp;

class SaveDataHistory : public DataHandlerPolicyBase
{
    protected:
        std::ofstream csvFile;
        virtual inet::Packet* handleDataMessage(inet::Ptr<const UsersInfoSnapshotMessage> received_packet) override;
    public:
        SaveDataHistory(RavensControllerApp* controllerApp, std::string path);
        virtual ~SaveDataHistory();
};

}

#endif /* "RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_" */
