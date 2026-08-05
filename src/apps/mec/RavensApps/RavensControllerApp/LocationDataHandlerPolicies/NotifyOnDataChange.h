#ifndef RAVENS_CONTROLLER_APP_NOTIFYONDATACHANGE_H_
#define RAVENS_CONTROLLER_APP_NOTIFYONDATACHANGE_H_

#include "LocationDataHandlerPolicyBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include <string>

// class RavensControllerApp;

namespace simu5g {

class NotifyOnDataChange : public LocationDataHandlerPolicyBase
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
        NotifyOnDataChange(RavensControllerApp* controllerApp);
        virtual ~NotifyOnDataChange(){}
};

}

#endif /* "RAVENS_CONTROLLER_APP_NOTIFYONDATACHANGE_H_" */
