#include "PredictionServerClient.h"
#include "apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.h"

namespace simu5g {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// A simulation time has no JSON type of its own. It is written as the decimal
// string, which is the same text the CSV writes, so a timestamp reads
// identically whether the model is being trained or served.
static nlohmann::json jsonValue(omnetpp::simtime_t value) { return value.str(); }

// Everything else goes through as-is: strings, whole numbers, doubles.
template <typename T>
static nlohmann::json jsonValue(const T& value) { return value; }

PredictionServerClient::PredictionServerClient(RavensControllerApp* controllerApp, const std::string& baseUrl)
    : TelemetrySink(controllerApp)
{
    predictUrl_ = baseUrl + "/predict";
    pendingUsers_ = nlohmann::json::array();
    pendingCells_ = nlohmann::json::array();

    // Reset server state at the start of each run. The server buffers a window
    // of observations per user, and those buffers outlive a simulation — without
    // this, a run is served predictions partly derived from the previous one.
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string resetUrl = baseUrl + "/reset";
        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, resetUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            std::cout << "[PredictionServerClient] server state reset: " << response << std::endl;
        } else {
            std::cout << "[PredictionServerClient] WARNING: reset failed: "
                      << curl_easy_strerror(res) << std::endl;
        }
        curl_easy_cleanup(curl);
    }
}

PredictionServerClient::~PredictionServerClient()
{
}

// One user's readings from the frame being processed, oldest first — a single
// reading, since a frame carries one observation per user.
//
// Still sent as a per-user sequence rather than a flat list, because the
// prediction server consumes per-user sequences. It is meant to hold a window
// and nothing more — never to work out which user or which host a reading
// belongs to. Sending readings already attributed by whoever observed them is
// what keeps that true.
//
// The fields come from the shared record, so this payload carries exactly what
// the CSV carries, under exactly the same names. That is the whole reason the
// record exists: the two are written in different runs and compared only
// through a trained model, so a field present on one side and missing on the
// other would never surface at runtime.
void PredictionServerClient::onUserSamples(const std::vector<UserSample>& samples)
{
    if (samples.empty())
        return;

    nlohmann::json samplesJson = nlohmann::json::array();
    for (const auto& sample : samples)
    {
        nlohmann::json sampleJson;
        sample.forEachField([&sampleJson](const char* name, const auto& value) {
            sampleJson[name] = jsonValue(value);
        });
        samplesJson.push_back(sampleJson);
    }

    nlohmann::json userJson;
    userJson["ueId"]    = samples.front().userId;
    userJson["samples"] = samplesJson;
    pendingUsers_.push_back(userJson);
}

// The cell readings the frame carries, or nothing before the RNIS has first
// replied.
//
// Same fields and same names as the radio-stats CSV, from the same record.
// They used to disagree: the column CellId was the key accessPointId, and
// DlPrbUsageCell was dlTotalPrbUsageCell.
void PredictionServerClient::onCellSamples(const std::vector<CellSample>& samples)
{
    for (const auto& sample : samples)
    {
        nlohmann::json sampleJson;
        sample.forEachField([&sampleJson](const char* name, const auto& value) {
            sampleJson[name] = jsonValue(value);
        });
        pendingCells_.push_back(sampleJson);
    }
}

// End of the frame: label it, send it, and hand whatever comes back to the
// Controller — which forwards it to the orchestrator immediately, because a
// prediction held for even a second is a second further ahead the model needed
// to have seen.
void PredictionServerClient::onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame)
{
    nlohmann::json payload;
    payload["mecHostId"] = frame->getMecHostId();
    payload["timestamp"] = frame->getTimeStamp().str();

    // No avgDistanceToAp among the cell readings: it is the mean of a value
    // every sample in this same payload already carries, so the server
    // computes it if it wants it — the same way the offline pipeline does,
    // from the same numbers.
    payload["users"] = std::move(pendingUsers_);
    payload["cellSamples"] = std::move(pendingCells_);
    pendingUsers_ = nlohmann::json::array();
    pendingCells_ = nlohmann::json::array();

    std::cout << simTime() << " - PredictionServerClient - sending, users: "
              << payload["users"].size() << std::endl;
    std::string response = postToServer(payload);
    std::cout << simTime() << " - PredictionServerClient - response (" << response.size()
              << " bytes): " << response << std::endl;

    controllerApp_->publishPredictions(parseResponse(response, frame->getTimeStamp()));
}

std::string PredictionServerClient::postToServer(const nlohmann::json& payload)
{
    std::string response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        EV << "PredictionServerClient::postToServer - Failed to init curl" << endl;
        return response;
    }

    std::string jsonString = payload.dump();
    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, predictUrl_.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cout << "PredictionServerClient::postToServer - curl error: "
                  << curl_easy_strerror(res) << std::endl;
        response.clear();
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

std::vector<MigrationPrediction> PredictionServerClient::parseResponse(const std::string& response,
                                                                       omnetpp::simtime_t observedAt)
{
    std::vector<MigrationPrediction> predictions;
    if (response.empty()) return predictions;

    try {
        nlohmann::json jsonResponse = nlohmann::json::parse(response);

        for (const auto& entry : jsonResponse)
        {
            MigrationPrediction prediction;
            prediction.ueAddress = entry["Address"].get<std::string>();

            int currentAPId = entry["AccessPointId"].get<int>();
            prediction.fromMEHId = "mecHost" + std::to_string(currentAPId);

            int nextAPId = entry["NextAccessPointId"].get<int>();
            prediction.toMEHId = (nextAPId == -1) ? "" : "mecHost" + std::to_string(nextAPId);

            // The server answers with a delay from now; everything downstream
            // wants the instant. Converting here means exactly one place knows
            // the reply is relative.
            prediction.observedAt = observedAt;
            prediction.expectedAt = simTime() + entry.value("Duration", 0.0);

            // Neither of these is sent by the current server. They default to
            // "unstated" rather than to a plausible-looking value, so a log full
            // of confidence -1 reads as a server that never said, instead of a
            // model that was never sure.
            prediction.confidence = entry.value("Confidence", -1.0);
            prediction.modelId    = entry.value("ModelId", std::string());

            predictions.push_back(prediction);
        }
    }
    catch (const nlohmann::json::exception& e) {
        EV << "PredictionServerClient::parseResponse - JSON parse error: " << e.what() << endl;
    }

    return predictions;
}

} // namespace simu5g
