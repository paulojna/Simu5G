#include "SendToExternalServer.h"

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

	SendToExternalServer::SendToExternalServer(RavensControllerApp* controllerApp): LocationDataHandlerPolicyBase(controllerApp)
	{
		flaskUrl_ = "http://localhost:5001/predict";
		pendingUsers_ = nlohmann::json::array();
		pendingCells_ = nlohmann::json::array();

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

	// One UE's readings from the frame being processed, oldest first.
	//
	// The grouping is kept rather than flattened because the prediction server
	// consumes per-UE sequences. It is meant to hold a window and nothing more —
	// never to work out which UE or which host a reading belongs to. Sending
	// sequences already assembled by whoever observed them is what keeps that true.
	//
	// The fields come from the shared record, so this payload carries exactly what
	// the CSV carries, under exactly the same names. That is the whole reason the
	// record exists: the two are written in different runs and compared only
	// through a trained model, so a field present on one side and missing on the
	// other would never surface at runtime.
	void SendToExternalServer::onUserSamples(const std::vector<UserSample>& samples)
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

	// Every cell reading the frame carries, oldest first. One sequence, because
	// the cell is one thing — unlike the users, who each have their own.
	//
	// Same fields and same names as the radio-stats CSV, from the same record.
	// They used to disagree: the column CellId was the key accessPointId, and
	// DlPrbUsageCell was dlTotalPrbUsageCell.
	void SendToExternalServer::onCellSamples(const std::vector<CellSample>& samples)
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

	// End of the frame: label it and send.
	void SendToExternalServer::onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame)
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

		std::cout << simTime() << " - SendToExternalServer - sending to Flask, users: " << payload["users"].size() << std::endl;
		std::string response = postToFlask(payload);
		std::cout << simTime() << " - SendToExternalServer - Flask response (" << response.size() << " bytes): " << response << std::endl;
		std::vector<MigrationPrediction> predictions = parseResponse(response);

		for (auto& pred : predictions)
			controllerApp_->migrationPredictions.insert_or_assign(pred.getUeAddress(), pred);
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

