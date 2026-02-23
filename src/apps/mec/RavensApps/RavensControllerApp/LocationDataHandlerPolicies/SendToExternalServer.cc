#include "SendToExternalServer.h"
#include "../DataUpdates/UserMEHUpdate.h"

namespace simu5g {
	SendToExternalServer::SendToExternalServer(RavensControllerApp* controllerApp): LocationDataHandlerPolicyBase(controllerApp)
	{
		flaskUrl_ = "http://localhost:5001/predict"; //
	}

	SendToExternalServer::~SendToExternalServer()
	{
	}

	inet::Packet* SendToExternalServer::handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet)
	{
		inet::Packet* pck = nullptr;
		std::cout << "WE RECEIVED STUFF IN SEND TO EXTERNAL SERVER" << std::endl;

		// 1. Detect exits (same as NotifyOnDataChange)
		std::vector<UserState> removedUsers = controllerApp_->removeInactiveUsers();
		for (auto& user : removedUsers)
		{
			UserMEHUpdate update;
			update.setLastMEHId(user.currentMEH);
			update.setNewMEHId("");
			update.setAddress(user.userId);
			addUserUpdate(update);
		}

		// 2. Detect entries (same as NotifyOnDataChange, but NO handover detection)
		auto updatedSnapshot = received_packet;
		for (const auto& user : updatedSnapshot->getUsers())
		{
			if (user.second.getDlNongbrDelayUe() == -1) {
				continue;
			}

			auto userIt = controllerApp_->userStateMap.find(user.first);
			if (userIt == controllerApp_->userStateMap.end())
			{
				// New user — notify MEO to instantiate app
				UserMEHUpdate update;
				update.setLastMEHId("");
				update.setNewMEHId(updatedSnapshot->getMecHostId());
				update.setAddress(user.second.getAddress());
				addUserUpdate(update);
			}
			// No else block — handovers are Flask's responsibility
		}

		// 3. Update state maps (needed for entry/exit detection)
		controllerApp_->updateMehStateMap(received_packet);
		controllerApp_->updateUserStateMap(received_packet);

		// 4. Forward snapshot to Flask and collect migration predictions
		nlohmann::json payload = formatSnapshot(received_packet);
		std::cout << simTime() << " - SendToExternalServer - sending to Flask, users: " << payload["users"].size() << std::endl;
		std::string response = postToFlask(payload);
		std::cout << simTime() << " - SendToExternalServer - Flask response (" << response.size() << " bytes): " << response << std::endl;
		std::vector<MigrationPrediction> predictions = parseResponse(response);

		for (auto& pred : predictions)
		{
			controllerApp_->migrationPredictions.insert_or_assign(pred.getUeAddress(), pred);
		}

		return pck;
	}

	void SendToExternalServer::addUserUpdate(UserMEHUpdate& update)
	{
		const std::string& address = update.getAddress();
		auto [it, inserted] = controllerApp_->userUpdates.insert_or_assign(address, update);

		if (inserted) {
			EV << "SendToExternalServer::addUserUpdate - user " << address << " added" << endl;
		} else {
			EV << "SendToExternalServer::addUserUpdate - user " << address << " updated" << endl;
		}
	}

	nlohmann::json SendToExternalServer::formatSnapshot(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> snapshot)
	{
	    nlohmann::json payload;

	    // Snapshot-level metadata
	    payload["mecHostId"] = snapshot->getMecHostId();
	    payload["timestamp"] = snapshot->getTimeStamp().str();

	    // Per-user data
	    nlohmann::json usersJson = nlohmann::json::array();
	    for (const auto& [ueId, userData] : snapshot->getUsers())
	    {
	        if (userData.getDlNongbrDelayUe() == -1) continue;

	        nlohmann::json userJson;

	        // Identity
	        userJson["ueId"] = ueId;
	        userJson["address"] = userData.getAddress();
	        userJson["accessPointId"] = userData.getAccessPointId();

	        // Timestamps
	        userJson["lastUpdated"] = userData.getLastUpdated().str();
	        userJson["lsUpdate"] = userData.getLsUpdate().str();
	        userJson["rnisUpdate"] = userData.getRnisUpdate().str();

	        // Location & mobility
	        userJson["x"] = userData.getCurrentLocation().getX();
	        userJson["y"] = userData.getCurrentLocation().getY();
	        userJson["z"] = userData.getCurrentLocation().getZ();
	        userJson["speed"] = userData.getCurrentLocation().getHorizontalSpeed();
	        userJson["bearing"] = userData.getCurrentLocation().getBearing();
	        userJson["distanceToAp"] = userData.getDistanceToAP();

	        // Per-UE radio stats (RNIS)
	        userJson["dlNongbrDelayUe"] = userData.getDlNongbrDelayUe();
	        userJson["dlNongbrPdrUe"] = userData.getDlNongbrPdrUe();
	        userJson["dlNongbrDataVolumeUe"] = userData.getDlNongbrDataVolumeUe();
	        userJson["ulNongbrDelayUe"] = userData.getUlNongbrDelayUe();
	        userJson["ulNongbrPdrUe"] = userData.getUlNongbrPdrUe();
	        userJson["ulNongbrDataVolumeUe"] = userData.getUlNongbrDataVolumeUe();

	        usersJson.push_back(userJson);
	    }

	    payload["users"] = usersJson;

	    return payload;
	}


	static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output)
	{
	    size_t totalSize = size * nmemb;
	    output->append((char*)contents, totalSize);
	    return totalSize;
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

