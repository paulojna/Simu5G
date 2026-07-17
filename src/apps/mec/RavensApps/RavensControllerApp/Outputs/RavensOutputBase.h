#ifndef RAVENS_CONTROLLER_APP_RAVENSOUTPUTBASE_H_
#define RAVENS_CONTROLLER_APP_RAVENSOUTPUTBASE_H_

#include "../RavensControllerApp.h"

namespace simu5g {

using namespace omnetpp;

class RavensControllerApp;

// Abstract base of the Controller's outputs. The Controller core owns the world
// model (userStateMap / mehStateMap) and publishes to the set of outputs selected
// by the "profile" parameter; each output consumes telemetry and/or lifecycle
// hooks and is otherwise independent (History = HistoryOutput; Prediction =
// HistoryOutput + MeoOutput + PredictionOutput; Reaction = MeoOutput).
class RavensOutputBase
{
    friend class RavensControllerApp;

    protected:
        RavensControllerApp* controllerApp_;

        // Called for every TELEMETRY_FRAME (FULL mode only), after the core has
        // refreshed the world model. Default is a no-op.
        virtual void onTelemetry(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) {}

        // Semantic hooks — called by handleEventFrame() at each authoritative decision point.
        // Default implementations are no-ops; outputs override only what they need.
        virtual void onUserEntry   (const std::string& userId, const std::string& meh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}
        virtual void onUserHandover(const std::string& userId, const std::string& fromMeh,
                                    const std::string& toMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}
        virtual void onUserExit    (const std::string& userId, const std::string& fromMeh,
                                    int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}

        // Shared helper: insert_or_assign a UserMEHUpdate into controllerApp_->userUpdates.
        // Used by MeoOutput only.
        void emitUserUpdate(const std::string& address,
                            const std::string& lastMeh,
                            const std::string& newMeh);

    public:
        RavensOutputBase(RavensControllerApp* controllerApp) { controllerApp_ = controllerApp; }
        virtual ~RavensOutputBase() {}
};

}

#endif /* RAVENS_CONTROLLER_APP_RAVENSOUTPUTBASE_H_ */
