#include "PredictionOutput.h"

namespace simu5g {

	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
	{
	    size_t totalSize = size * nmemb;
	    output->append((char*)contents, totalSize);
	    return totalSize;
	}

	PredictionOutput::PredictionOutput(RavensControllerApp* controllerApp, std::string runDir): RavensOutputBase(controllerApp)
	{
		flaskUrl_ = "http://localhost:5001/predict";

		if (!runDir.empty()) {
			std::string runNumber = std::to_string(getEnvir()->getConfigEx()->getActiveRunNumber());
			std::string logName = runDir + "run_" + runNumber + "_predictions.jsonl";
			predictionsLog_.open(logName, std::ios::out | std::ios::trunc);
			EV << "PredictionOutput initialized. Predictions log: " << logName << endl;
		}

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
				std::cout << "[PredictionOutput] Flask state reset: " << response << std::endl;
			} else {
				std::cout << "[PredictionOutput] WARNING: Flask reset failed: "
				          << curl_easy_strerror(res) << std::endl;
			}
			curl_easy_cleanup(curl);
		}
	}

	PredictionOutput::~PredictionOutput()
	{
		if (predictionsLog_.is_open())
			predictionsLog_.close();
	}

	void PredictionOutput::onTelemetry(inet::Ptr<const RavensLinkDataFrameMessage> received_packet)
	{
		// Forward data frame to Flask and collect migration predictions
		nlohmann::json payload = formatSnapshot(received_packet);
		std::cout << simTime() << " - PredictionOutput - sending to Flask, users: " << payload["users"].size() << std::endl;
		std::string response = postToFlask(payload);
		std::cout << simTime() << " - PredictionOutput - Flask response (" << response.size() << " bytes): " << response << std::endl;

		// Log the call verbatim — the ground truth for the offline prediction
		// evaluation. Empty response = failed/timed-out call, logged as "".
		if (predictionsLog_.is_open()) {
			nlohmann::json logLine;
			logLine["t"] = simTime().str();
			logLine["sourceMeh"] = received_packet->getMecHostId();
			try {
				logLine["response"] = nlohmann::json::parse(response);
			} catch (const nlohmann::json::exception&) {
				logLine["response"] = response;  // not valid JSON — keep raw string
			}
			predictionsLog_ << logLine.dump() << "\n";
			predictionsLog_.flush();
		}

		std::vector<MigrationPrediction> predictions = parseResponse(response);

		for (auto& pred : predictions)
			controllerApp_->migrationPredictions.insert_or_assign(pred.getUeAddress(), pred);
	}

	nlohmann::json PredictionOutput::formatSnapshot(inet::Ptr<const RavensLinkDataFrameMessage> snapshot)
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

	    // Per-user LS state (no RNIS fields — dropped in Piece 3)
	    nlohmann::json usersJson = nlohmann::json::array();
	    for (const auto& [ueId, userData] : snapshot->getUsers())
	    {
	        nlohmann::json userJson;
	        userJson["ueId"]          = ueId;
	        userJson["address"]       = userData.getAddress();
	        userJson["accessPointId"] = userData.getAccessPointId();
	        userJson["x"]             = userData.getCurrentLocation().getX();
	        userJson["y"]             = userData.getCurrentLocation().getY();
	        userJson["z"]             = userData.getCurrentLocation().getZ();
	        userJson["speed"]         = userData.getCurrentLocation().getHorizontalSpeed();
	        userJson["bearing"]       = userData.getCurrentLocation().getBearing();
	        userJson["distanceToAp"]  = userData.getDistanceToAP();
	        usersJson.push_back(userJson);
	    }
	    payload["users"] = usersJson;

	    return payload;
	}


	std::string PredictionOutput::postToFlask(const nlohmann::json& payload)
	{
	    std::string response;
	    CURL* curl = curl_easy_init();
	    if (!curl) {
	        EV << "PredictionOutput::postToFlask - Failed to init curl" << endl;
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
	        std::cout << "PredictionOutput::postToFlask - curl error: " << curl_easy_strerror(res) << std::endl;
	        response.clear();
	    }

	    curl_slist_free_all(headers);
	    curl_easy_cleanup(curl);
	    return response;
	}

	std::vector<MigrationPrediction> PredictionOutput::parseResponse(const std::string& response)
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
	        EV << "PredictionOutput::parseResponse - JSON parse error: " << e.what() << endl;
	    }

	    return predictions;
	}

} // namespace simu5g
