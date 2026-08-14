#include "PredictionServerClient.h"
#include "apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.h"
#include "nodes/mec/utils/httpUtils/JsonValue.h"

namespace simu5g {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

PredictionServerClient::PredictionServerClient(RavensControllerApp* controllerApp,
                                               const std::string& baseUrl,
                                               double timeoutSeconds)
    : TelemetrySink(controllerApp),
      predictUrl_(baseUrl + "/predict"),
      resetUrl_(baseUrl + "/reset"),
      timeoutSeconds_((long)timeoutSeconds)
{
    curl_ = curl_easy_init();
    if (curl_ == nullptr)
        throw cRuntimeError("PredictionServerClient - could not initialise curl");

    headers_ = curl_slist_append(nullptr, "Content-Type: application/json");

    // Set once for the handle's lifetime. Only the URL and the body change per
    // request, which is what lets the connection be reused.
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, timeoutSeconds_);

    try {
        resetServer();
    }
    catch (...) {
        // A destructor does not run for an object whose constructor threw, so
        // the handle is released here or not at all.
        curl_slist_free_all(headers_);
        curl_easy_cleanup(curl_);
        throw;
    }
}

PredictionServerClient::~PredictionServerClient()
{
    if (headers_ != nullptr)
        curl_slist_free_all(headers_);
    if (curl_ != nullptr)
        curl_easy_cleanup(curl_);
}

/*
    Opens the run against the server, and does two things that both have to
    succeed before a single prediction is served.

    It clears the server's per-user buffers. The server holds a window of
    observations per user and those buffers outlive a simulation, so without this
    a run is served predictions partly derived from the previous one.

    It agrees the protocol version. The server is developed separately and in
    another repository, so the payload this build sends and the payload that
    build expects can drift apart with nothing to notice — and a server reading a
    payload it does not understand answers plausible nonsense, which looks
    exactly like a bad model.

    Every failure here ends the run, and this is the one place on this boundary
    that refuses to proceed. It is the opposite of what a failed prediction does,
    for a reason: a missing prediction degrades a proactive run to a reactive
    one, which is survivable and still measures something. A run that starts
    against a server holding another run's history, or speaking another payload,
    produces plausible numbers that are wrong — which is worse than producing
    none, because nothing downstream can tell.
*/
void PredictionServerClient::resetServer()
{
    nlohmann::json payload;
    payload["protocolVersion"] = PROTOCOL_VERSION;

    // Held for the whole call: curl does not copy the body, it reads from this
    // buffer while the request is in flight.
    const std::string body = payload.dump();
    std::string response;

    curl_easy_setopt(curl_, CURLOPT_URL, resetUrl_.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

    CURLcode result = curl_easy_perform(curl_);
    if (result != CURLE_OK)
        throw cRuntimeError("PredictionServerClient - %s unreachable: %s. A proactive run needs "
                            "the model server; start it, or run a configuration that does not "
                            "consume predictions.",
                            resetUrl_.c_str(), curl_easy_strerror(result));

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200)
        throw cRuntimeError("PredictionServerClient - %s answered HTTP %ld. The server refuses "
                            "protocol version %d.",
                            resetUrl_.c_str(), httpCode, PROTOCOL_VERSION);

    nlohmann::json reply;
    try {
        reply = nlohmann::json::parse(response);
    }
    catch (const nlohmann::json::exception& e) {
        throw cRuntimeError("PredictionServerClient - %s returned a body that is not JSON: %s",
                            resetUrl_.c_str(), e.what());
    }

    // The acknowledgement is required rather than checked when present. A server
    // that says nothing about the version is one that predates the handshake,
    // and treating its silence as agreement would leave exactly the mismatch
    // this call exists to catch.
    if (!reply.contains("protocolVersion") || !reply["protocolVersion"].is_number_integer())
        throw cRuntimeError("PredictionServerClient - %s did not acknowledge a protocol version. "
                            "It predates the version handshake and cannot be trusted to read this "
                            "payload.", resetUrl_.c_str());

    int serverVersion = reply["protocolVersion"].get<int>();
    if (serverVersion != PROTOCOL_VERSION)
        throw cRuntimeError("PredictionServerClient - protocol mismatch: this build speaks %d, "
                            "%s speaks %d.", PROTOCOL_VERSION, resetUrl_.c_str(), serverVersion);

    EV << "PredictionServerClient::resetServer - " << resetUrl_ << " reset, protocol version "
       << PROTOCOL_VERSION << endl;
}

/*
    A closed telemetry window, sent to the model server as one request, and its
    answer handed back to the Controller.

    ONE REQUEST, NOT ONE PER HOST
    Every host's rows for this interval travel together. Sent per frame, a user
    crossing between hosts inside one interval was split across two requests and
    the model never saw the crossing — which is the one thing in the window most
    worth seeing. It also cost one blocking call per host per interval.

    FLAT ROWS, IN THE RECORD'S OWN NAMES
    One row per (user, observing host), exactly as the CSV writes them, under
    exactly the same keys. Nothing is grouped, ordered or derived here: the
    server does that, with the same code it uses on the CSV when it trains. Two
    implementations of the same grouping is how a training set and a served
    payload drift apart without anything at runtime revealing it.

    WHY THE REPORTING HOSTS ARE CARRIED
    A host that observed nobody and a host whose frame was lost both produce no
    rows, and telemetry rides UDP, so the second really happens. Without this
    list the model would read a lost frame as every user on that host having
    vanished.

    Sent even when the window is empty. The server keeps a per-user history and
    advances it by window; a window silently skipped is a gap in that history
    that looks like time not passing.
*/
void PredictionServerClient::onTelemetryWindow(omnetpp::simtime_t windowStart,
                                               omnetpp::simtime_t windowEnd,
                                               const std::vector<std::string>& reportingMEHs,
                                               const std::vector<UserSample>& userSamples,
                                               const std::vector<CellSample>& cellSamples)
{
    nlohmann::json payload;
    payload["windowStart"]   = jsonValue(windowStart);
    payload["windowEnd"]     = jsonValue(windowEnd);
    payload["reportingMEHs"] = reportingMEHs;

    // No top-level host id: a window spans hosts, and every row names the host
    // that observed it anyway.
    nlohmann::json userRows = nlohmann::json::array();
    for (const auto& sample : userSamples)
    {
        nlohmann::json row;
        sample.forEachField([&row](const char* name, const auto& value) {
            row[name] = jsonValue(value);
        });
        userRows.push_back(std::move(row));

        // The newest moment this user was actually looked at, which is what a
        // prediction about it will carry as its observedAt. A max rather than
        // the last row seen: rows sit in the order the hosts' frames arrived
        // over the network, which is not the order they were observed in.
        auto seen = newestObservation_.find(sample.userId);
        if (seen == newestObservation_.end())
            newestObservation_.emplace(sample.userId, sample.locationTimestamp);
        else if (seen->second < sample.locationTimestamp)
            seen->second = sample.locationTimestamp;
    }

    // No avgDistanceToAp among the cell readings: it is the mean of a value
    // every row in this same payload already carries, so the server computes it
    // if it wants it — the same way the offline pipeline does, from the same
    // numbers.
    nlohmann::json cellRows = nlohmann::json::array();
    for (const auto& sample : cellSamples)
    {
        nlohmann::json row;
        sample.forEachField([&row](const char* name, const auto& value) {
            row[name] = jsonValue(value);
        });
        cellRows.push_back(std::move(row));
    }

    payload["userSamples"] = std::move(userRows);
    payload["cellSamples"] = std::move(cellRows);

    EV << "PredictionServerClient::onTelemetryWindow - [" << windowStart << ", " << windowEnd
       << "] sending " << userSamples.size() << " user rows, " << cellSamples.size()
       << " cell rows, from " << reportingMEHs.size() << " hosts" << endl;

    requests_++;
    std::string response;
    if (!postToServer(payload, response)) {
        // No predictions this window, and the run carries on — a proactive run
        // missing a prediction degrades to a reactive one, which is survivable
        // and still measures something. What must not happen is it passing
        // unrecorded, so the count goes in the warning and into a scalar at the
        // end of the run.
        noAnswer_++;
        EV_WARN << "PredictionServerClient::onTelemetryWindow - no answer from " << predictUrl_
                << " (" << noAnswer_ << " of " << requests_ << " requests so far); this window is "
                << "served no predictions" << endl;
        return;
    }

    std::vector<MigrationPrediction> predictions = parseResponse(response);
    predictions_ += predictions.size();

    EV << "PredictionServerClient::onTelemetryWindow - " << predictions.size()
       << " predictions from " << response.size() << " bytes" << endl;

    // Handed over rather than published: the Controller holds them for the
    // model's inference time before the orchestrator sees them.
    controllerApp_->deliverPredictions(predictions);
}

// False means the server did not answer at all — unreachable, or slower than
// the timeout. Distinct from answering with nothing, which is a model saying it
// sees no move coming, and distinct again from the reset above, which ends the
// run rather than returning.
bool PredictionServerClient::postToServer(const nlohmann::json& payload, std::string& response)
{
    // Held for the whole call, for the reason given in resetServer().
    const std::string body = payload.dump();
    response.clear();

    // The size is set explicitly on every request. The handle is shared with the
    // reset call, and curl remembers what it was last told: a body length left
    // over from another payload would silently truncate this one.
    curl_easy_setopt(curl_, CURLOPT_URL, predictUrl_.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, (long)body.size());
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        EV_WARN << "PredictionServerClient::postToServer - " << predictUrl_ << ": "
                << curl_easy_strerror(res) << endl;
        response.clear();
        return false;
    }

    return true;
}

/*
    The reply, in the same vocabulary the request is written in.

    It used to speak its own: the request sent UEId and MEHId while the reply
    answered Address, AccessPointId and NextAccessPointId. One name per quantity
    end to end is the rule the shared record exists to enforce, and the reply is
    now inside it.

    Two other things the reply used to do, and no longer does:

    - Name hosts by number. NextAccessPointId was turned into a host name by
      writing "mecHost" + the number, which assumes access point N is served by
      mecHost N. That holds in tust_1to1 and is a property of nothing. Host names
      now cross verbatim, as they do on the orchestrator's engine boundary.

    - Answer a delay. Duration was added to simTime() here, which was correct
      only because the call blocked and simTime() had not moved — true by
      coincidence rather than by construction, and no longer true at all now that
      the answer waits out inferenceTime before anyone reads it. The server
      answers the instant, matching MigrationPrediction, the decision log and the
      engine boundary.

    A required field that is missing throws, which is caught below and counted as
    a malformed reply. Confidence and ModelId are optional and default to
    "unstated" rather than to a plausible-looking value, so a log full of
    confidence -1 reads as a server that never said, rather than as a model that
    was never sure.
*/
std::vector<MigrationPrediction> PredictionServerClient::parseResponse(const std::string& response)
{
    std::vector<MigrationPrediction> predictions;

    // An empty body is not treated as "no predictions": the server answered, so
    // it owes a JSON array, and an empty array is how it says nothing is coming.
    // Silence at this point is a broken reply and is counted as one.
    try {
        nlohmann::json jsonResponse = nlohmann::json::parse(response);

        for (const auto& entry : jsonResponse)
        {
            MigrationPrediction prediction;
            prediction.ueAddress = entry.at("UEId").get<std::string>();
            prediction.fromMEHId = entry.at("MEHId").get<std::string>();

            // Empty means a predicted departure from the system rather than a
            // move, which is what isExitPrediction() reads.
            prediction.toMEHId   = entry.at("NextMEHId").get<std::string>();

            prediction.expectedAt = entry.at("ExpectedAt").get<double>();

            // How stale the information behind this prediction is, taken per
            // user rather than per window — see newestObservation_. Unknown only
            // for a user no window ever carried, which means the server answered
            // about somebody the simulation never showed it.
            auto seen = newestObservation_.find(prediction.ueAddress);
            if (seen != newestObservation_.end()) {
                prediction.observedAt = seen->second;
            }
            else {
                EV_WARN << "PredictionServerClient::parseResponse - prediction for "
                        << prediction.ueAddress << ", which no window has carried" << endl;
            }

            prediction.confidence = entry.value("Confidence", -1.0);
            prediction.modelId    = entry.value("ModelId", std::string());

            predictions.push_back(prediction);
        }
    }
    catch (const nlohmann::json::exception& e) {
        // Counted apart from an unanswered request. After the version handshake
        // at /reset the two ends agree on the payload, so a reply this build
        // cannot read is a server fault rather than version drift — and either
        // way it must not read as a model with nothing to say.
        malformedReply_++;
        EV_WARN << "PredictionServerClient::parseResponse - unreadable reply from " << predictUrl_
                << " (" << malformedReply_ << " so far): " << e.what() << endl;
        predictions.clear();
    }

    return predictions;
}

// What the proactive arm actually got. Read together: requests answered with
// predictions, against requests that went unanswered or came back broken, is how
// much of a run configured as proactive was proactive in fact.
void PredictionServerClient::onRunFinished()
{
    controllerApp_->recordScalar("predictionRequests", requests_);
    controllerApp_->recordScalar("predictionRequestsUnanswered", noAnswer_);
    controllerApp_->recordScalar("predictionRepliesMalformed", malformedReply_);
    controllerApp_->recordScalar("predictionsReceived", predictions_);
}

} // namespace simu5g
