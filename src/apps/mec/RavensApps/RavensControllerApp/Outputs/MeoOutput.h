#ifndef RAVENS_CONTROLLER_APP_MEOOUTPUT_H_
#define RAVENS_CONTROLLER_APP_MEOOUTPUT_H_

#include "RavensOutputBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include <string>

namespace simu5g {

// Reports UE lifecycle to the MEO: turns entry/handover/exit hooks into
// UserMEHUpdate entries, flushed by the Controller's periodic snapshot.
// Active in the Prediction and Reaction profiles.
class MeoOutput : public RavensOutputBase
{
    protected:
        virtual void onUserEntry   (const std::string& userId, const std::string& meh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
        virtual void onUserHandover(const std::string& userId, const std::string& fromMeh,
                                    const std::string& toMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
        virtual void onUserExit    (const std::string& userId, const std::string& fromMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
    public:
        MeoOutput(RavensControllerApp* controllerApp);
        virtual ~MeoOutput(){}
};

}

#endif /* RAVENS_CONTROLLER_APP_MEOOUTPUT_H_ */
