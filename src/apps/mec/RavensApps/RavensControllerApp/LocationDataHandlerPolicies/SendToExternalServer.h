#ifndef RAVENS_CONTROLLER_APP_SENDTOEXTERNALSERVER_H_
#define RAVENS_CONTROLLER_APP_SENDTOEXTERNALSERVER_H_

#include "LocationDataHandlerPolicyBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include "../DataUpdates/MigrationPrediction.h"

#include "nodes/mec/utils/httpUtils/json.hpp"
#include <curl/curl.h>
#include <string>

namespace simu5g {

	class SendToExternalServer : public LocationDataHandlerPolicyBase
	{
	private:
		std::string flaskUrl_;

		// The contents of the frame currently being processed, waiting for the
		// frame hook to send them. Filled and drained inside a single frame — not
		// buffers: nothing is ever held across frames or reordered.
		nlohmann::json pendingUsers_;
		nlohmann::json pendingCells_;

		std::string postToFlask(const nlohmann::json& payload);
		std::vector<MigrationPrediction> parseResponse(const std::string& response);

	protected:
		virtual void onUserSamples(const std::vector<UserSample>& samples) override;
		virtual void onCellSamples(const std::vector<CellSample>& samples) override;
		virtual void onTelemetryFrame(inet::Ptr<const RavensLinkDataFrameMessage> frame) override;
		virtual void onUserEntry   (const std::string& userId, const std::string& meh,
		                            int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
		virtual void onUserHandover(const std::string& userId, const std::string& fromMeh,
		                            const std::string& toMeh,
		                            int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;
		virtual void onUserExit    (const std::string& userId, const std::string& fromMeh,
		                            int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) override;

	public:
		SendToExternalServer(RavensControllerApp* controllerApp);
		virtual ~SendToExternalServer();
	};

}

#endif
