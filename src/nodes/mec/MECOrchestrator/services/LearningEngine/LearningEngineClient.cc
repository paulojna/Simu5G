#include "LearningEngineClient.h"

#include "omnetpp.h"

namespace simu5g {

using namespace omnetpp;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output)
{
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

LearningEngineClient::LearningEngineClient(const std::string& baseUrl, double timeoutSeconds)
    : resetUrl_(baseUrl + "/reset"),
      stepUrl_(baseUrl + "/step"),
      timeoutSeconds_((long)timeoutSeconds)
{
    curl_ = curl_easy_init();
    if (curl_ == nullptr)
        throw cRuntimeError("LearningEngineClient - could not initialise curl");

    headers_ = curl_slist_append(nullptr, "Content-Type: application/json");

    // Set once for the handle's lifetime. Only the URL and the body change per
    // request, which is what lets the connection be reused between steps.
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, timeoutSeconds_);
}

LearningEngineClient::~LearningEngineClient()
{
    if (headers_ != nullptr)
        curl_slist_free_all(headers_);
    if (curl_ != nullptr)
        curl_easy_cleanup(curl_);
}

void LearningEngineClient::openEpisode(const nlohmann::json& topology)
{
    nlohmann::json payload = topology;
    payload["protocolVersion"] = PROTOCOL_VERSION;

    nlohmann::json reply = post(resetUrl_, payload);

    if (!reply.contains("episodeId"))
        throw cRuntimeError("LearningEngineClient - engine answered /reset without an episodeId");

    episodeId_ = reply["episodeId"].get<std::string>();
    stepIndex_ = 0;

    EV << "LearningEngineClient::openEpisode - episode " << episodeId_ << " open at "
       << resetUrl_ << endl;
}

nlohmann::json LearningEngineClient::step(nlohmann::json observation)
{
    observation["episodeId"] = episodeId_;
    observation["step"]      = stepIndex_;
    observation["terminal"]  = false;

    nlohmann::json reply = post(stepUrl_, observation);
    stepIndex_++;

    // Absent and empty mean the same thing and are both allowed: every user
    // waits this step. What is not allowed is arriving here without a reply at
    // all, which post() has already turned into an error.
    if (!reply.contains("actions"))
        return nlohmann::json::array();

    if (!reply["actions"].is_array())
        throw cRuntimeError("LearningEngineClient - step %d: 'actions' is not an array",
                            stepIndex_ - 1);

    return reply["actions"];
}

void LearningEngineClient::closeEpisode(nlohmann::json observation)
{
    if (episodeId_.empty())
        return;

    observation["episodeId"] = episodeId_;
    observation["step"]      = stepIndex_;
    observation["terminal"]  = true;

    post(stepUrl_, observation);

    EV << "LearningEngineClient::closeEpisode - episode " << episodeId_ << " closed after "
       << stepIndex_ << " steps" << endl;

    episodeId_.clear();
}

nlohmann::json LearningEngineClient::post(const std::string& url, const nlohmann::json& payload)
{
    // Held for the whole call: curl does not copy the body, it reads from this
    // buffer while the request is in flight.
    const std::string body = payload.dump();

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, (long)body.size());

    std::string response;
    CURLcode result = CURLE_OK;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++)
    {
        response.clear();
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);

        result = curl_easy_perform(curl_);
        if (result == CURLE_OK)
            break;

        EV_WARN << "LearningEngineClient - " << url << " attempt " << attempt << " of "
                << MAX_ATTEMPTS << " failed: " << curl_easy_strerror(result) << endl;
    }

    if (result != CURLE_OK)
        throw cRuntimeError("LearningEngineClient - %s unreachable after %d attempts: %s",
                            url.c_str(), MAX_ATTEMPTS, curl_easy_strerror(result));

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200)
        throw cRuntimeError("LearningEngineClient - %s answered HTTP %ld", url.c_str(), httpCode);

    try {
        return nlohmann::json::parse(response);
    }
    catch (const nlohmann::json::exception& e) {
        throw cRuntimeError("LearningEngineClient - %s returned a body that is not JSON: %s",
                            url.c_str(), e.what());
    }
}

} // namespace simu5g
