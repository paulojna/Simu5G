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
        int msgCount_ = 0;
        static const int FLUSH_INTERVAL_ = 100;
        virtual void onUserSamples(const std::vector<UserSample>& samples) override;
        virtual void onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame) override;
        virtual void onUserEntry   (const std::string& userId, const std::string& meh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
        virtual void onUserHandover(const std::string& userId, const std::string& fromMeh,
                                    const std::string& toMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
        virtual void onUserExit    (const std::string& userId, const std::string& fromMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
    public:
        SaveDataHistory(RavensControllerApp* controllerApp, std::string path);
        virtual ~SaveDataHistory();
};

}

#endif /* "RAVENS_CONTROLLER_APP_SAVEDATAHISTORY_H_" */
