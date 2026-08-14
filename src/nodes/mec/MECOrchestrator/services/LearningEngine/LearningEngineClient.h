#ifndef __LEARNINGENGINECLIENT_H_
#define __LEARNINGENGINECLIENT_H_

#include "nodes/mec/utils/httpUtils/json.hpp"
#include <curl/curl.h>
#include <string>

namespace simu5g {

// Carries one episode of the decision loop to the reinforcement-learning engine
// outside the simulation, and brings its actions back.
//
// The engine connects to the orchestrator rather than to the Controller because
// its observation includes where the applications are and what state they are in
// — the application view, which RAVENS never sees — and what it returns are
// actions, which are the orchestrator's to execute.
//
// The engine is a persistent server that outlives any single run: the weights
// have to carry across episodes, so a run is one episode against a server that
// was already there. That is why the episode identity is assigned by the server
// and echoed back on every step, rather than being the connection itself —
// several runs may train against one engine at the same time.
//
// Every call blocks the simulation while it is in flight, deliberately and for
// the same reason PredictionServerClient does: the time the engine takes to
// answer is a real term in whether a migration can still finish before the user
// arrives, and letting simulated time run on during the call would hide it.
//
// WHAT THIS CLASS OWNS
// The envelope, not the content. It stamps the episode id, the step number and
// the terminal marker onto whatever observation it is handed; assembling the
// observation itself belongs to the strategy, which is the only thing that knows
// what the two views contain.
class LearningEngineClient {
  public:
    // Sent in the reset payload and checked by the engine, which refuses an
    // episode it does not recognise. Both ends of this boundary evolve on their
    // own schedules, and a shape mismatch discovered at the first step reads as
    // a broken agent rather than as a stale server. Bump on any change to the
    // payloads that is not purely additive.
    static constexpr int PROTOCOL_VERSION = 1;

    // baseUrl is the engine root; the endpoints are baseUrl + "/reset" and
    // baseUrl + "/step". timeoutSeconds needs to be generous: a training step
    // runs a gradient update, which is far slower than the inference the
    // prediction server does.
    LearningEngineClient(const std::string& baseUrl, double timeoutSeconds);
    ~LearningEngineClient();

    LearningEngineClient(const LearningEngineClient&) = delete;
    LearningEngineClient& operator=(const LearningEngineClient&) = delete;

    // Starts the episode, handing over the parts of the setup that do not change
    // while it runs: the hosts, their cells and capacities, the cell positions,
    // and the run's configuration. Sent once so that per-step payloads carry only
    // what varies, and so the engine derives candidate geometry from positions it
    // already holds instead of being told distances every window.
    //
    // Stores the episode id the server answers with.
    void openEpisode(const nlohmann::json& topology);

    // One decision step. Returns the "actions" array — possibly empty, which is a
    // legitimate answer meaning every user waits.
    //
    // Throws on no answer. An unanswered step must never be read as an empty
    // action list: the two are indistinguishable downstream, and a fabricated
    // all-wait would be paired with a real transition and corrupt what the engine
    // learns from it. A run that cannot reach its engine has to stop.
    nlohmann::json step(nlohmann::json observation);

    // The terminal marker, carrying the last step's reward so the final
    // transition is complete. The reply holds no actions and is discarded — the
    // run is over by the time it arrives.
    void closeEpisode(nlohmann::json observation);

    const std::string& getEpisodeId() const { return episodeId_; }
    int getStepCount() const { return stepIndex_; }

  private:
    std::string resetUrl_;
    std::string stepUrl_;
    std::string episodeId_;
    long timeoutSeconds_;

    // Counts the steps sent, and is what the engine orders transitions by. Owned
    // here rather than by the strategy so that the step number cannot disagree
    // with the number of requests actually made.
    int stepIndex_ = 0;

    // One handle for the client's lifetime, so curl keeps the connection alive
    // between steps instead of opening a new one for each. At one request per
    // telemetry window over a 3600-second run this is thousands of handshakes
    // saved per episode.
    CURL* curl_ = nullptr;
    curl_slist* headers_ = nullptr;

    // How many times a request is attempted before the run is abandoned. A
    // transport error is retried because a momentarily unreachable engine is not
    // the same as a wrong one, and a training campaign is hundreds of executions
    // long — one dropped connection should not cost a whole run. A refusal by
    // the engine (any non-200) is not retried: the server answered, and asking
    // again would only get the same answer.
    static constexpr int MAX_ATTEMPTS = 3;

    // Posts the payload and returns the parsed reply. Throws cRuntimeError once
    // the attempts are spent, on a non-200 status, or on a body that is not
    // JSON — all of which mean the same thing here, which is that this step has
    // no answer.
    nlohmann::json post(const std::string& url, const nlohmann::json& payload);
};

} // namespace simu5g

#endif
