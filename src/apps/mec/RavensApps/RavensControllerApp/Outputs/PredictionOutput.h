#ifndef RAVENS_CONTROLLER_APP_PREDICTIONOUTPUT_H_
#define RAVENS_CONTROLLER_APP_PREDICTIONOUTPUT_H_

#include "RavensOutputBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include "../DataUpdates/MigrationPrediction.h"

#include "nodes/mec/utils/httpUtils/json.hpp"
#include <curl/curl.h>
#include <string>

namespace simu5g {

	// Forwards telemetry frames to the external prediction server (Flask) and
	// feeds the returned migration predictions into the Controller's snapshot
	// toward the MEO. Active in the Prediction profile only; lifecycle reporting
	// to the MEO is MeoOutput's job.
	class PredictionOutput : public RavensOutputBase
	{
	private:
		std::string flaskUrl_;

		nlohmann::json formatSnapshot(inet::Ptr<const RavensLinkDataFrameMessage> snapshot);
		std::string postToFlask(const nlohmann::json& payload);
		std::vector<MigrationPrediction> parseResponse(const std::string& response);

	protected:
		virtual void onTelemetry(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) override;

	public:
		PredictionOutput(RavensControllerApp* controllerApp);
		virtual ~PredictionOutput();
	};

}

#endif /* RAVENS_CONTROLLER_APP_PREDICTIONOUTPUT_H_ */
