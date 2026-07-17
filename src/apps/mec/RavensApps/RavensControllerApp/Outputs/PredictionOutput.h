#ifndef RAVENS_CONTROLLER_APP_PREDICTIONOUTPUT_H_
#define RAVENS_CONTROLLER_APP_PREDICTIONOUTPUT_H_

#include "RavensOutputBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include "../DataUpdates/MigrationPrediction.h"

#include "nodes/mec/utils/httpUtils/json.hpp"
#include <curl/curl.h>
#include <fstream>
#include <string>

namespace simu5g {

	// Forwards telemetry frames to the external prediction server (Flask) and
	// feeds the returned migration predictions into the Controller's snapshot
	// toward the MEO. Active in the Prediction profile only; lifecycle reporting
	// to the MEO is MeoOutput's job.
	//
	// Every Flask call is logged verbatim to run_<N>_predictions.jsonl (one JSON
	// object per line: t, sourceMeh, response). The log is schema-free on purpose:
	// the prediction model's output format may change, and the offline evaluation
	// parses it per-model. An empty "response" records a failed/timed-out call.
	class PredictionOutput : public RavensOutputBase
	{
	private:
		std::string flaskUrl_;
		std::ofstream predictionsLog_;

		nlohmann::json formatSnapshot(inet::Ptr<const RavensLinkDataFrameMessage> snapshot);
		std::string postToFlask(const nlohmann::json& payload);
		std::vector<MigrationPrediction> parseResponse(const std::string& response);

	protected:
		virtual void onTelemetry(inet::Ptr<const RavensLinkDataFrameMessage> received_packet) override;

	public:
		// runDir: per-run results directory, created and owned by the Controller
		PredictionOutput(RavensControllerApp* controllerApp, std::string runDir);
		virtual ~PredictionOutput();
	};

}

#endif /* RAVENS_CONTROLLER_APP_PREDICTIONOUTPUT_H_ */
