#include "SendToExternalServer.h"

namespace simu5g {

	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
	{
	    size_t totalSize = size * nmemb;
	    output->append((char*)contents, totalSize);
	    return totalSize;
	}

	SendToExternalServer::SendToExternalServer(RavensControllerApp* controllerApp): LocationDataHandlerPolicyBase(controllerApp)
	{
		flaskUrl_ = "http://localhost:5001/predict";

		// Reset Flask state at the start of each simulation run
		// to prevent stale UE buffers from previous runs
		CURL* curl = curl_easy_init();
		if (curl) {
			std::string resetUrl = "http://localhost:5001/reset";
			std::string response;
			curl_easy_setopt(curl, CURLOPT_URL, resetUrl.c_str());
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
			CURLcode res = curl_easy_perform(curl);
			if (res == CURLE_OK) {
				std::cout << "[SendToExternalServer] Flask state reset: " << response << std::endl;
			} else {
				std::cout << "[SendToExternalServer] WARNING: Flask reset failed: "
				          << curl_easy_strerror(res) << std::endl;
			}
			curl_easy_cleanup(curl);
		}
	}

	SendToExternalServer::~SendToExternalServer()
	{
	}

	inet::Packet* SendToExternalServer::handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
	{
		// Last-resort cleanup: drop users nothing has been heard about for far
		// longer than any normal gap. Departures are normally confirmed through
		// the event channel long before this, so this should stay quiet — it
		// exists so a user whose exit event was somehow never delivered cannot
		// linger indefinitely.
		std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
		for (const auto& user : removedUsers)
			onUserExit(user.userId, user.currentMEH, -1, SIMTIME_ZERO);

		// Forward data frame to Flask and collect migration predictions
		nlohmann::json payload = formatSnapshot(received_packet);
		std::cout << simTime() << " - SendToExternalServer - sending to Flask, users: " << payload["users"].size() << std::endl;
		std::string response = postToFlask(payload);
		std::cout << simTime() << " - SendToExternalServer - Flask response (" << response.size() << " bytes): " << response << std::endl;
		std::vector<MigrationPrediction> predictions = parseResponse(response);

		for (auto& pred : predictions)
			controllerApp_->migrationPredictions.insert_or_assign(pred.getUeAddress(), pred);

		return nullptr;
	}

	void SendToExternalServer::onUserEntry(const std::string& userId, const std::string& meh,
	                                       int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
	{
		EV << "SendToExternalServer::onUserEntry - " << userId << " at " << meh << endl;
		emitUserUpdate(userId, "", meh);
	}

	void SendToExternalServer::onUserHandover(const std::string& userId,
	                                          const std::string& fromMeh, const std::string& toMeh,
	                                          int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
	{
		EV << "SendToExternalServer::onUserHandover - " << userId << " " << fromMeh << " -> " << toMeh << endl;
		emitUserUpdate(userId, fromMeh, toMeh);
	}

	void SendToExternalServer::onUserExit(const std::string& userId, const std::string& fromMeh,
	                                      int /*samplesSinceChange*/, omnetpp::simtime_t /*firstDetectedAt*/)
	{
		EV << "SendToExternalServer::onUserExit - " << userId << " left " << fromMeh << endl;
		emitUserUpdate(userId, fromMeh, "");
	}

	nlohmann::json SendToExternalServer::formatSnapshot(inet::Ptr<const RavensLinkDataFrameMessage> snapshot)
	{
	    nlohmann::json payload;

	    payload["mecHostId"] = snapshot->getMecHostId();
	    payload["timestamp"] = snapshot->getTimeStamp().str();

	    // Cell-level radio aggregates (replaces removed per-user RNIS fields)
	    const AccessPointRadioInfoData& ap = snapshot->getApRadioInfo();
	    nlohmann::json cellJson;
	    cellJson["accessPointId"]                  = ap.getAccessPointId();
	    cellJson["dlTotalPrbUsageCell"]             = ap.getDlTotalPrbUsageCell();
	    cellJson["ulTotalPrbUsageCell"]             = ap.getUlTotalPrbUsageCell();
	    cellJson["dlNongbrPdrCell"]                 = ap.getDlNongbrPdrCell();
	    cellJson["ulNongbrPdrCell"]                 = ap.getUlNongbrPdrCell();
	    cellJson["numberOfActiveUeDlNongbrCell"]    = ap.getNumberOfActiveUeDlNongbrCell();
	    cellJson["avgDlDelay"]                      = ap.getAvgDlDelay();
	    cellJson["avgUlDelay"]                      = ap.getAvgUlDelay();
	    cellJson["totalDlDataVolume"]               = ap.getTotalDlDataVolume();
	    cellJson["totalUlDataVolume"]               = ap.getTotalUlDataVolume();
	    cellJson["avgDistanceToAp"]                 = ap.getAvgDistanceToAp();
	    payload["cellMetrics"] = cellJson;

	    // Per-user location readings, grouped as they arrived: one entry per user,
	    // holding every reading taken since the previous frame, oldest first.
	    //
	    // The grouping is passed on rather than flattened because the prediction
	    // server consumes per-user sequences. It is meant to buffer a window and
	    // nothing more — never to work out which user or which host a reading
	    // belongs to. Sending sequences already assembled by whoever observed them
	    // is what keeps that true.
	    //
	    // Each reading carries its own timestamp. Without one they would be
	    // indistinguishable in time, which would make the input meaningless to a
	    // sequence model now that a single frame spans several seconds.
	    nlohmann::json usersJson = nlohmann::json::array();
	    for (const auto& group : snapshot->getUserSamples())
	    {
	        nlohmann::json samplesJson = nlohmann::json::array();
	        for (const auto& sample : group.samples)
	        {
	            nlohmann::json sampleJson;
	            sampleJson["locationTimestamp"] = sample.getTimestamp().str();
	            sampleJson["accessPointId"]     = sample.getAccessPointId();
	            sampleJson["x"]                 = sample.getCurrentLocation().getX();
	            sampleJson["y"]                 = sample.getCurrentLocation().getY();
	            sampleJson["z"]                 = sample.getCurrentLocation().getZ();
	            sampleJson["speed"]             = sample.getCurrentLocation().getHorizontalSpeed();
	            sampleJson["bearing"]           = sample.getCurrentLocation().getBearing();
	            sampleJson["distanceToAp"]      = sample.getDistanceToAP();
	            samplesJson.push_back(sampleJson);
	        }

	        nlohmann::json userJson;
	        userJson["ueId"]    = group.ueAddress;
	        userJson["samples"] = samplesJson;
	        usersJson.push_back(userJson);
	    }
	    payload["users"] = usersJson;

	    return payload;
	}


	std::string SendToExternalServer::postToFlask(const nlohmann::json& payload)
	{
	    std::string response;
	    CURL* curl = curl_easy_init();
	    if (!curl) {
	        EV << "SendToExternalServer::postToFlask - Failed to init curl" << endl;
	        return response;
		}

	    std::string jsonString = payload.dump();
	    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");

	    curl_easy_setopt(curl, CURLOPT_URL, flaskUrl_.c_str());
	    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonString.c_str());
	    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
	    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

	    CURLcode res = curl_easy_perform(curl);
	    if (res != CURLE_OK) {
	        std::cout << "SendToExternalServer::postToFlask - curl error: " << curl_easy_strerror(res) << std::endl;
	        response.clear();
	    }

	    curl_slist_free_all(headers);
	    curl_easy_cleanup(curl);
	    return response;
	}

	std::vector<MigrationPrediction> SendToExternalServer::parseResponse(const std::string& response)
	{
		std::vector<MigrationPrediction> predictions;
		if (response.empty()) return predictions;

	    try {
	        nlohmann::json jsonResponse = nlohmann::json::parse(response);

	        for (const auto& entry : jsonResponse)
	        {
	            std::string ueAddress = entry["Address"].get<std::string>();

	            int currentAPId = entry["AccessPointId"].get<int>();
	            std::string currentMEHId = "mecHost" + std::to_string(currentAPId);

	            int nextAPId = entry["NextAccessPointId"].get<int>();
	            std::string targetMEHId = (nextAPId == -1) ? "" : "mecHost" + std::to_string(nextAPId);

	            double delay = entry.value("Duration", 0.0);

	            predictions.emplace_back(ueAddress, currentMEHId, targetMEHId, delay, simTime());
	        }
	    }
	    catch (const nlohmann::json::exception& e) {
	        EV << "SendToExternalServer::parseResponse - JSON parse error: " << e.what() << endl;
	    }

	    return predictions;
	}

} // namespace simu5g

