#ifndef RAVENS_CONTROLLER_APP_HISTORYOUTPUT_H_
#define RAVENS_CONTROLLER_APP_HISTORYOUTPUT_H_

#include "RavensOutputBase.h"

#include <fstream>
#include <iostream>
#include <string>

namespace simu5g {

using namespace omnetpp;

// Records the ground truth of a run as CSVs: per-user telemetry, cell radio
// aggregates, and the UE lifecycle (entry/handover/exit). Active in the History
// and Prediction profiles; files go to <path>/<profileDir>/run_<N>/.
class HistoryOutput : public RavensOutputBase
{
    protected:
        std::ofstream userFile;
        std::ofstream lifecycleFile;
        std::ofstream radioStatsFile;
        int msgCount_ = 0;
        static const int FLUSH_INTERVAL_ = 100;
        virtual void onTelemetry(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) override;
        virtual void onUserEntry   (const std::string& userId, const std::string& meh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
        virtual void onUserHandover(const std::string& userId, const std::string& fromMeh,
                                    const std::string& toMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
        virtual void onUserExit    (const std::string& userId, const std::string& fromMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
    public:
        // profileDir separates runs per profile (e.g. "history", "prediction")
        HistoryOutput(RavensControllerApp* controllerApp, std::string path, std::string profileDir);
        virtual ~HistoryOutput();
};

}

#endif /* RAVENS_CONTROLLER_APP_HISTORYOUTPUT_H_ */
