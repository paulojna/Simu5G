#ifndef RAVENS_CONTROLLER_APP_PREDICTIONSERVERCLIENT_H_
#define RAVENS_CONTROLLER_APP_PREDICTIONSERVERCLIENT_H_

#include "TelemetrySink.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/MigrationPrediction.h"

#include "nodes/mec/utils/httpUtils/json.hpp"
#include <curl/curl.h>
#include <map>
#include <string>

namespace simu5g {

// Ships each telemetry window to a model server outside the simulation and turns
// its reply into predictions. Was SendToExternalServer.
//
// One request per window, not per frame. Per frame meant one request per host
// per interval — some tens of thousands of blocking calls in a run — and, worse
// than the cost, it meant the model saw fragments: a user crossing between hosts
// inside one interval was split across two requests, and the pair of rows that
// says so is the most informative thing in the window for a model whose job is
// predicting the next handover.
//
// The request blocks the process while it is in flight, which costs wall-clock
// but no simulated time. The model's inference time is therefore not measured
// here at all — it is declared as inferenceTime and applied by the Controller
// when it delivers the answer. See RavensControllerApp::deliverPredictions().
//
// WHAT THIS CLASS DOES NOT DO
// Shape the data. The rows sent are the canonical records exactly as the CSV
// writes them, and everything downstream of that — grouping rows into per-user
// sequences, windowing, derived features — belongs to the server, which does the
// same work offline when it trains. Two implementations of "which rows belong to
// this user, in what order" is how the training set and the served payload come
// to disagree without anything at runtime revealing it.
//
// Nothing here knows which model is on the other end. The reply may name one,
// and that name is carried through to the logs for grouping runs, but it never
// reaches a decision — which is what allows models to be added and swapped
// without touching this boundary.
class PredictionServerClient : public TelemetrySink
{
    public:
        // Sent in the reset payload and checked by the server, which refuses a
        // run it does not recognise. Both ends of this boundary evolve on their
        // own schedules and the server lives in another repository, so a shape
        // mismatch found at the first prediction reads as a bad model rather
        // than as a stale server — the same failure mode as a server that is
        // quietly unreachable. Bump on any change to the payloads that is not
        // purely additive.
        //
        //   1  one request per frame, users nested under per-user sequences,
        //      top-level mecHostId, reply keyed by Address / AccessPointId /
        //      NextAccessPointId / Duration
        //   2  one request per window, flat rows, window bounds and the
        //      reporting hosts, reply in the request's own vocabulary with an
        //      absolute ExpectedAt and host names verbatim
        static constexpr int PROTOCOL_VERSION = 2;

    private:
        std::string predictUrl_;
        std::string resetUrl_;

        // The newest observation of each user, over the whole run.
        //
        // This is what a prediction's observedAt is taken from, and it is per
        // user because a window is not one instant: it holds a second's worth of
        // rows from every host, and a user seen just after the window opened is
        // a great deal staler than one seen just before it closed. Stamping both
        // with the window's end would understate the first by up to a whole
        // interval — and which user that happens to depends only on where its
        // host's frame fell in the window, so it is not even a bias that could
        // be subtracted out afterwards.
        //
        // Kept for the run rather than the window, because the server holds its
        // own per-user history and can answer about a user this window did not
        // contain. "Three seconds old" is exactly what the orchestrator should
        // be told there; resetting each window would only be able to say
        // "unknown".
        std::map<std::string, omnetpp::simtime_t> newestObservation_;

        // One handle for the client's lifetime, so curl keeps the connection
        // alive between requests instead of shaking hands again for each one.
        // LearningEngineClient holds its handle the same way; the two sides of
        // this boundary should not differ in how they talk.
        CURL* curl_ = nullptr;
        curl_slist* headers_ = nullptr;

        // How long a request may take before it is abandoned, from
        // predictionServerTimeout. A policy rather than a constant: it is where
        // a run decides a slow server is a failed one.
        long timeoutSeconds_;

        // What this run's proactive arm actually got, recorded as scalars at the
        // end of it.
        //
        // Without these a run in which the server was slow, restarted, or
        // briefly unreachable is indistinguishable from one in which the model
        // had nothing to say — and both are indistinguishable from a model that
        // is simply bad. That is a measurement problem in the arm whose entire
        // purpose is to measure a model, so each cause is counted apart from the
        // others:
        //
        //   requests_       every window a request was made for
        //   noAnswer_       the server did not answer: unreachable, or slower
        //                   than timeoutSeconds_
        //   malformedReply_ it answered something this build cannot read, which
        //                   after the version handshake means a server bug
        //                   rather than drift
        //   predictions_    predictions actually received; against requests_
        //                   this is how proactive the run was
        //
        // A window that was asked and answered with nothing is the difference
        // between requests_ and the three below it — the model looking and
        // seeing no move coming, which is a legitimate answer and the one thing
        // here that is not a fault.
        long requests_ = 0;
        long noAnswer_ = 0;
        long malformedReply_ = 0;
        long predictions_ = 0;

        // Opens the run against the server. Fatal on any failure; see the
        // definition for why this boundary is the one that refuses to proceed.
        void resetServer();

        // Sends the payload and writes the body into response. False means no
        // answer, which is not the same as an empty one and is counted apart
        // from it.
        bool postToServer(const nlohmann::json& payload, std::string& response);

        // Each prediction's observedAt is looked up per user in
        // newestObservation_, which is why no timestamp is passed in.
        std::vector<MigrationPrediction> parseResponse(const std::string& response);

    protected:
        virtual void onTelemetryWindow(omnetpp::simtime_t windowStart,
                                       omnetpp::simtime_t windowEnd,
                                       const std::vector<std::string>& reportingMEHs,
                                       const std::vector<UserSample>& userSamples,
                                       const std::vector<CellSample>& cellSamples) override;
        virtual void onRunFinished() override;

    public:
        PredictionServerClient(RavensControllerApp* controllerApp, const std::string& baseUrl,
                               double timeoutSeconds);
        virtual ~PredictionServerClient();

        // The handle is owned, so copying one of these would close it twice.
        PredictionServerClient(const PredictionServerClient&) = delete;
        PredictionServerClient& operator=(const PredictionServerClient&) = delete;
};

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_PREDICTIONSERVERCLIENT_H_ */
