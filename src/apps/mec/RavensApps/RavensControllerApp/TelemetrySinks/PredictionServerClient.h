#ifndef RAVENS_CONTROLLER_APP_PREDICTIONSERVERCLIENT_H_
#define RAVENS_CONTROLLER_APP_PREDICTIONSERVERCLIENT_H_

#include "TelemetrySink.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/MigrationPrediction.h"

#include "nodes/mec/utils/httpUtils/json.hpp"
#include <curl/curl.h>
#include <string>

namespace simu5g {

// Ships each telemetry frame to a model server outside the simulation and turns
// its reply into predictions. Was SendToExternalServer.
//
// The request blocks the simulation while it is in flight. That is deliberate —
// the model's inference time is a real term in whether a proactive migration can
// finish before the user arrives, and letting simulated time run on during the
// call would hide it. It is also, by a wide margin, the most expensive thing in
// any run configured this way.
//
// Nothing here knows which model is on the other end. The reply may name one,
// and that name is carried through to the logs for grouping runs, but it never
// reaches a decision — which is what allows models to be added and swapped
// without touching this boundary.
class PredictionServerClient : public TelemetrySink
{
    private:
        std::string predictUrl_;

        // The frame currently being assembled, waiting for the frame hook to
        // send it. Filled and drained inside a single frame — not a buffer:
        // nothing is ever held across frames or reordered.
        nlohmann::json pendingUsers_;
        nlohmann::json pendingCells_;

        std::string postToServer(const nlohmann::json& payload);

        // observedAt is the frame's own timestamp: the newest observation the
        // model can have seen when it answered.
        std::vector<MigrationPrediction> parseResponse(const std::string& response,
                                                       omnetpp::simtime_t observedAt);

    protected:
        virtual void onUserSamples(const std::vector<UserSample>& samples) override;
        virtual void onCellSamples(const std::vector<CellSample>& samples) override;
        virtual void onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame) override;

    public:
        PredictionServerClient(RavensControllerApp* controllerApp, const std::string& baseUrl);
        virtual ~PredictionServerClient();
};

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_PREDICTIONSERVERCLIENT_H_ */
