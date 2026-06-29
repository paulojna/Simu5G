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

		nlohmann::json formatSnapshot(inet::Ptr<const RavensLinkDataFrameMessage> snapshot);
		std::string postToFlask(const nlohmann::json& payload);
		std::vector<MigrationPrediction> parseResponse(const std::string& response);

	protected:
		virtual inet::Packet* handleDataMessage(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) override;

	public:
		SendToExternalServer(RavensControllerApp* controllerApp);
		void addUserUpdate(UserMEHUpdate& update);
		virtual ~SendToExternalServer();
	};

}

#endif
