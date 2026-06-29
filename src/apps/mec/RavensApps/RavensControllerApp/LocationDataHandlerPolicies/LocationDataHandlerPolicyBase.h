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

        virtual inet::Packet* handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) = 0;

        // Semantic hooks — called by handleEventFrame() at each authoritative decision point.
        // Default implementations are no-ops; policies override only what they need.
        virtual void onUserEntry   (const std::string& userId, const std::string& meh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}
        virtual void onUserHandover(const std::string& userId, const std::string& fromMeh,
                                    const std::string& toMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}
        virtual void onUserExit    (const std::string& userId, const std::string& fromMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}

        // Shared helper: insert_or_assign a UserMEHUpdate into controllerApp_->userUpdates.
        // Not for SaveDataHistory (log-only).
        void emitUserUpdate(const std::string& address,
                            const std::string& lastMeh,
                            const std::string& newMeh);

    public:
        LocationDataHandlerPolicyBase(RavensControllerApp* controllerApp) { controllerApp_ = controllerApp; }
        virtual ~LocationDataHandlerPolicyBase() {}
};

}

#endif /* RAVENS_CONTROLLER_APP_LOCATIONDATAHANDLERPOLICYBASE_H_ */
