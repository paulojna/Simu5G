#include "ObservationBuilder.h"

#include "nodes/mec/utils/httpUtils/JsonValue.h"

#include <map>

namespace simu5g {

nlohmann::json ObservationBuilder::buildTopology(const std::vector<HostTopology>& hosts,
                                                 const std::vector<CellTopology>& cells,
                                                 const nlohmann::json& config) const
{
    nlohmann::json payload;
    payload["config"] = config;

    nlohmann::json hostsJson = nlohmann::json::array();
    for (const auto& host : hosts) {
        nlohmann::json hostJson;
        hostJson["host"]    = host.host;
        hostJson["cells"]   = host.cells;
        hostJson["isCloud"] = host.isCloud;
        hostJson["maxRam"]  = host.maxRam;
        hostsJson.push_back(hostJson);
    }
    payload["hosts"] = hostsJson;

    nlohmann::json cellsJson = nlohmann::json::array();
    for (const auto& cell : cells) {
        nlohmann::json cellJson;
        cellJson["cell"] = cell.cell;
        cellJson["x"]    = cell.x;
        cellJson["y"]    = cell.y;
        cellsJson.push_back(cellJson);
    }
    payload["cells"] = cellsJson;

    return payload;
}

nlohmann::json ObservationBuilder::buildUsers() const
{
    // Keyed by the canonical user address, which both views already use. A user
    // may appear in one view and not the other: a user seen by RAVENS before its
    // application exists has presence and no app, and an application whose user
    // has exited has an app row and a presence row with an empty currentMEH.
    // Both are legitimate and both are sent.
    std::map<std::string, nlohmann::json> rows;

    for (const auto& presence : api_->getUserPresence()) {
        nlohmann::json presenceJson;
        presenceJson["currentMEH"]      = presence.currentMEH;
        presenceJson["lastEventAt"]     = jsonValue(presence.lastEventAt);
        presenceJson["lastObservedMEH"] = presence.lastObservedMEH;
        presenceJson["lastSampleAt"]    = jsonValue(presence.lastSampleAt);

        rows[presence.ueAddress]["ueAddress"] = presence.ueAddress;
        rows[presence.ueAddress]["presence"]  = presenceJson;
    }

    for (const auto& placement : api_->getAppPlacements()) {
        nlohmann::json appJson;
        appJson["meh"]       = placement.mecHost;
        appJson["state"]     = appStateName(placement.state);
        appJson["contextId"] = placement.contextId;

        rows[placement.ueAddress]["ueAddress"] = placement.ueAddress;
        rows[placement.ueAddress]["app"]       = appJson;
    }

    nlohmann::json usersJson = nlohmann::json::array();
    for (const auto& row : rows)
        usersJson.push_back(row.second);

    return usersJson;
}

nlohmann::json ObservationBuilder::buildStep(omnetpp::simtime_t windowStart,
                                             omnetpp::simtime_t windowEnd,
                                             const std::vector<std::string>& reportingMEHIds,
                                             const std::vector<UserSample>& userSamples,
                                             const std::vector<CellSample>& cellSamples,
                                             const StepContext& context) const
{
    nlohmann::json payload;
    payload["windowStart"] = jsonValue(windowStart);
    payload["windowEnd"]   = jsonValue(windowEnd);

    // Which hosts actually reported, carried rather than inferred from the
    // samples: a host that saw no users and a host whose frame was lost both
    // contribute zero rows, and only this list tells them apart.
    payload["reportingMEHs"] = reportingMEHIds;

    payload["users"] = buildUsers();

    nlohmann::json eventsJson = nlohmann::json::array();
    for (const auto& event : context.events) {
        nlohmann::json eventJson;
        eventJson["ueAddress"]  = event.ueAddress;
        eventJson["type"]       = userEventTypeName(event.eventType);
        eventJson["fromMEH"]    = event.fromMEHId;
        eventJson["toMEH"]      = event.toMEHId;
        eventJson["observedAt"] = jsonValue(event.observedAt);
        eventsJson.push_back(eventJson);
    }
    payload["events"] = eventsJson;

    // The raw window, under the field names the CSVs use and the prediction
    // server already receives. One name per quantity end to end: a feature the
    // engine trains on is the same string as the column it was recorded under.
    nlohmann::json userSamplesJson = nlohmann::json::array();
    for (const auto& sample : userSamples) {
        nlohmann::json sampleJson;
        sample.forEachField([&sampleJson](const char* name, const auto& value) {
            sampleJson[name] = jsonValue(value);
        });
        userSamplesJson.push_back(sampleJson);
    }
    payload["userSamples"] = userSamplesJson;

    nlohmann::json cellSamplesJson = nlohmann::json::array();
    for (const auto& sample : cellSamples) {
        nlohmann::json sampleJson;
        sample.forEachField([&sampleJson](const char* name, const auto& value) {
            sampleJson[name] = jsonValue(value);
        });
        cellSamplesJson.push_back(sampleJson);
    }
    payload["cellSamples"] = cellSamplesJson;

    nlohmann::json predictionsJson = nlohmann::json::array();
    for (const auto& prediction : context.predictions) {
        nlohmann::json predictionJson;
        predictionJson["ueAddress"]  = prediction.ueAddress;
        predictionJson["fromMEH"]    = prediction.fromMEHId;
        predictionJson["toMEH"]      = prediction.toMEHId;
        predictionJson["observedAt"] = jsonValue(prediction.observedAt);
        predictionJson["expectedAt"] = jsonValue(prediction.expectedAt);
        predictionJson["confidence"] = prediction.confidence;
        predictionJson["modelId"]    = prediction.modelId;
        predictionsJson.push_back(predictionJson);
    }
    payload["predictions"] = predictionsJson;

    // Predicted per-cell occupancy. Empty today because nothing produces it
    // yet, and it will arrive the same way the per-user predictions above do —
    // from the Controller, which is where the models that describe future
    // observations live. The orchestrator never asks a model anything; it
    // receives what the Controller forwards.
    //
    // Sent empty rather than omitted so the engine parses the same shape from
    // the first episode, and filling it later reaches the engine without either
    // side changing.
    payload["cellPredictions"] = nlohmann::json::array();

    payload["rejectedActions"] = context.rejectedActions;

    return payload;
}

} // namespace simu5g
