#include "RavensAgentApp.h"

#include "apps/mec/RavensApps/RavensLinkSizes.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/Packet_m.h"

#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"

#include "nodes/mec/utils/httpUtils/httpUtils.h"
#include "nodes/mec/utils/httpUtils/json.hpp"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpRequestMessage/HttpRequestMessage.h"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpResponseMessage/HttpResponseMessage.h"

#include <filesystem>
#include <map>
#include <unordered_set>

namespace simu5g {

using namespace inet;
Define_Module(RavensAgentApp);

RavensAgentApp::RavensAgentApp(): MecAppBase()
{
    this->localSnapshotCounter = 0;
}

RavensAgentApp::~RavensAgentApp()
{
}

void RavensAgentApp::initialize(int stage)
{
    MecAppBase::initialize(stage);

    //avoiding multiple initializations
    if(stage != inet::INITSTAGE_APPLICATION_LAYER)
        return;

    EV << "RavensAgentApp::initialize - Mec application "<< getClassName() << " with mecAppId["<< mecAppId << "] has started! " <<  endl;
    EV << "RavensAgentApp::initialize - Initializing MecAppBase variables such as the mp1Port -->" << mp1Port << endl;
    mp1Socket_ = addNewSocket();

    controllerPort = par("controllerDataPort");
    controllerMgmtPort_ = par("controllerMgmtPort");
    localPort_ = par("localPort");

    accessPoints = std::vector<AccessPointData>();
    users = std::unordered_map<std::string, UserData>();

    this->mecHostId = mecHost->getName();

    telemetryInterval_ = par("telemetryInterval");
    agentMode_ = FULL_MODE; // default until ACK received

    entryConfirmSamples_ = par("entryConfirmSamples");
    exitConfirmSamples_ = par("exitConfirmSamples");

    // connection to the RAVENS CONTROLLER
    auto *msg = new cMessage("connectRC");
    scheduleAt(simTime() + 0.5, msg);
}

void RavensAgentApp::finish()
{
    MecAppBase::finish();
    EV << "RavensAgentApp::finish()" << endl;

    // Recorded as scalars rather than only logged: these end up in the run's
    // result file, so how often frames grew past the path limit is a number
    // that can be read straight off a batch of runs.
    recordScalar("telemetryFramesSent", telemetryFramesSent_);
    recordScalar("telemetryFramesOversized", telemetryFramesOversized_);

    if(gate("socketOut")->isConnected()){

    }
}

/**
 * Callback invoked when a TCP socket connection is successfully established.
 * Routes to appropriate setup logic based on socket type: MP1 (service discovery),
 * LS (access point query), or RNIS (L2 measurement subscription).
 */
void RavensAgentApp::established(int connId)
{
    if(connId == mp1Socket_->getSocketId())
    {
        EV << "RavensAgentApp::established - Mp1Socket" << endl;
    	std::string host = mp1Socket_->getRemoteAddress().str()+":"+std::to_string(mp1Socket_->getRemotePort());

        // get endpoint for the location service
        const char *location_service_uri = "/example/mec_service_mgmt/v1/services?ser_name=LocationService";
        Http::sendGetRequest(mp1Socket_, host.c_str(), location_service_uri);
    	EV << "RavensAgentApp::established - Request Sent to " << host.c_str() << " to " << location_service_uri << endl;

        // RAVENS V3 - Using RNIS besides LS
        // Immediately request RNIS service discovery to parallelize setup
    	const char *rnis_service_uri = "/example/mec_service_mgmt/v1/services?ser_name=RNIService";
    	Http::sendGetRequest(mp1Socket_, host.c_str(), rnis_service_uri);
        EV << mecHostId << " - RavensAgentApp::established - Request Sent to " << host.c_str() << " to " << rnis_service_uri << endl;
        return;
    }
    else if (connId == lsSocket_->getSocketId())
    {
        EV << "RavensAgentApp::established - lsSocket"<< endl;
        sendAPListRequest();
        return;
    }
	else if (connId == rnisSocket_->getSocketId())
	{
        // RAVENS V3 - Using RNIS besides LS
        // Send initial query to RNIS for Layer 2 measurements
		EV << mecHostId << " - RavensAgentApp::established - rnisSocket"<< endl;
		cMessage *msg = new cMessage("sendL2MeasSub");
		scheduleAt(simTime() + 0, msg);
		return;
	}
    else
    {
        throw cRuntimeError("RavenAgentApp::socketEstablished - Socket %d not recognized", connId);
    }
}

/**
* Sends a join request to the RAVENS Controller to register this MEC agent.
* Includes the MEC host identifier so the controller knows where the specific agent is located.
*/
void RavensAgentApp::sendJoinNetworkRequest()
{
    EV << "RavensAgentApp::sendJoinNetworkRequest - Sending Join Network Request" << endl;
    inet::Packet* packet = new inet::Packet("RavensLinkJoinNetworkRequestMessage");
    auto request = inet::makeShared<RavensLinkJoinNetworkRequestMessage>();
    request->setChunkLength(joinRequestBytes());
    request->setType(JOIN_NETWORK_REQUEST);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    // send mechostid from the mecHost of the MecAppBase
    request->setMecHostId(getMecHostId().c_str());
    packet->insertAtBack(request);
    controllerMgmtSocket_.send(packet);
}

/**
* Sends the list of discovered access points to the RAVENS Controller.
* This provides the controller with infrastructure topology information for this MEC host.
*/
void RavensAgentApp::sendAPList()
{
    EV << "RavensAgentApp::sendAPList - Sending AP List" << endl;
    inet::Packet* packet = new inet::Packet("RavensLinkInfrastructureDetailsMessage");
    auto request = inet::makeShared<RavensLinkInfrastructureDetailsMessage>();
    // Size grows with the number of access points this host reports.
    request->setChunkLength(infrastructureFrameBytes(accessPoints));
    request->setType(INFRASTRUCTURE_DETAILS);
    request->setRequestId(0);
    request->setTimeStamp(simTime());
    request->setMecHostId(getMecHostId().c_str());
    request->setAPList(accessPoints);
    packet->insertAtBack(request);
    controllerMgmtSocket_.send(packet);
}

/**
 * Sends an EVENT_FRAME reporting newEvents — the ENTRY/EXIT changes that
 * crossed their confirmation threshold at the LS tick that just ran — together
 * with anything still queued in pendingUnsent_ from a previous attempt. Called
 * once per LS tick from handleLSMessage, whether or not that tick produced any
 * new events, so a previously-failed send gets a chance to retry. No-op if
 * there is nothing new and nothing queued — there is no heartbeat/keepalive
 * behavior, and no TTL purge; a stable user that neither enters nor exits is
 * never reported again after its initial ENTRY ("report-once").
 *
 * Events are sent over the TCP signaling channel (controllerMgmtSocket_, kept
 * open after the config handshake): report-once semantics make event loss
 * unrecoverable, so they need reliable in-order delivery (TELEMETRY_FRAMEs
 * stay on UDP: loss-tolerant, superseded by the next sample). If the channel
 * is not connected, newEvents joins pendingUnsent_ and both are retried on the
 * next LS tick.
 */
void RavensAgentApp::sendEventFrame(const RavensEventList& newEvents)
{
    pendingUnsent_.insert(pendingUnsent_.end(), newEvents.begin(), newEvents.end());

    if (pendingUnsent_.empty())
        return;

    if (controllerMgmtSocket_.getState() != inet::TcpSocket::CONNECTED)
    {
        EV_WARN << mecHostId << " - RavensAgentApp::sendEventFrame - signaling channel not connected; "
                << "holding " << pendingUnsent_.size() << " event(s) for retry" << endl;
        return;
    }

    inet::Packet* packet = new inet::Packet("RavensLinkEventMessage");
    auto chunk = inet::makeShared<RavensLinkEventMessage>();
    // Size grows with the number of state changes being reported. This frame is
    // small by design - its cheapness against periodic polling is the point.
    chunk->setChunkLength(eventFrameBytes(pendingUnsent_));
    chunk->setType(EVENT_FRAME);
    chunk->setRequestId(localSnapshotCounter++);
    chunk->setTimeStamp(simTime());
    chunk->setMecHostId(getMecHostId().c_str());
    chunk->setEvents(pendingUnsent_);
    packet->insertAtBack(chunk);
    controllerMgmtSocket_.send(packet);

    EV << mecHostId << " - RavensAgentApp::sendEventFrame - sent " << pendingUnsent_.size() << " events over TCP" << endl;

    pendingUnsent_.clear();
}

/**
 * Sends the periodic telemetry frame: everything the Location Service reported
 * since the previous frame, grouped per UE, plus the cell-level radio
 * aggregates.
 *
 * The frame timer and the sensing rate are independent. The Location Service is
 * read every second and each reading is buffered; this function ships the whole
 * buffer and empties it. So the number of frames is unchanged from when only the
 * newest value was sent — there is simply more inside each one, and no
 * observation is discarded for having been taken between two frames.
 *
 * Emptying the buffer here, and only here, is what lets a departing UE's last
 * observations still be delivered: by the time the UE is erased from the current
 * -state map, its samples already belong to the buffer.
 */
void RavensAgentApp::sendDataFrame()
{
    if (agentMode_ != FULL_MODE) {
        // Nothing will ever drain these buffers in this mode. They are normally
        // empty already, since observations are only buffered in full mode — but
        // the Agent starts in full mode and learns its real mode from the
        // handshake reply, so anything collected before that reply is dropped here.
        sampleBuffer_.clear();
        cellSampleBuffer_.clear();
        return;
    }

    // No average distance to the serving cell is computed here any more. It was
    // the one value in the cell record that came from the Location Service
    // rather than the RNIS, and it is exactly recomputable downstream: every
    // sample in this frame carries its own DistanceToAccessPoint, so the mean is
    // a group-by — at one-second resolution rather than one value per frame, and
    // over observations that genuinely share a timestamp.
    //
    // It was also averaging the wrong population. It ran over the current-state
    // map at send time, which excludes UEs that have already departed, while the
    // frame below still carries those UEs' observations. The value therefore
    // matched no group anyone could reconstruct from the frame it travelled in.

    // Hand the buffered observations over to the wire form, one group per UE.
    // The samples are moved rather than copied — the buffer is being emptied
    // either way, and a busy host can be holding a few hundred of them.
    UeSampleGroupList groups;
    groups.reserve(sampleBuffer_.size());
    int sampleCount = 0;
    for (auto& [address, samples] : sampleBuffer_) {
        sampleCount += (int)samples.size();
        UeSampleGroup group;
        group.ueAddress = address;
        group.samples = std::move(samples);
        groups.push_back(std::move(group));
    }
    sampleBuffer_.clear();

    inet::Packet* packet = new inet::Packet("RavensLinkDataFrameMessage");
    auto chunk = inet::makeShared<RavensLinkDataFrameMessage>();
    // Size grows with the observations actually carried, on both sides: a UE seen
    // three times this interval costs three samples, and three RNIS replies cost
    // three cell records. Before the first RNIS reply arrives there are none, so
    // the earliest frames of a run are legitimately smaller - which the old bool
    // could not express, since it was true from initialize() onwards.
    inet::B frameBytes = telemetryFrameBytes(groups, cellSampleBuffer_.size());
    chunk->setChunkLength(frameBytes);
    chunk->setType(TELEMETRY_FRAME);
    chunk->setRequestId(localSnapshotCounter++);
    chunk->setTimeStamp(simTime());
    chunk->setMecHostId(getMecHostId().c_str());
    chunk->setUserSamples(groups);
    chunk->setCellSamples(cellSampleBuffer_);
    int cellSampleCount = (int)cellSampleBuffer_.size();
    cellSampleBuffer_.clear();
    packet->insertAtBack(chunk);
    controllerSocket_.send(packet);
    telemetryFramesSent_++;

    // Past this size the network layer splits the frame and the receiver
    // reassembles it, which on this path is safe (see TELEMETRY_PATH_MTU_B).
    // The warning and the count are here to make it visible how often busy
    // hosts cross the line, not to stop them doing it.
    if (frameBytes > inet::B(TELEMETRY_PATH_MTU_B)) {
        telemetryFramesOversized_++;
        EV_WARN << mecHostId << " - RavensAgentApp::sendDataFrame - frame of "
                << frameBytes << " exceeds the " << TELEMETRY_PATH_MTU_B
                << " B path limit and will be split up: " << groups.size()
                << " users, " << sampleCount << " samples, "
                << cellSampleCount << " cell readings" << endl;
    }

    EV << mecHostId << " - RavensAgentApp::sendDataFrame - sent telemetry frame with "
       << groups.size() << " users, " << sampleCount << " samples, "
       << cellSampleCount << " cell readings, " << frameBytes << endl;
}


/**
* Processes HTTP responses from the MP1 service registry interface.
* Parses service discovery responses to extract LocationService and RNIService
* endpoints, then schedules socket connections to the discovered services.
*/
void RavensAgentApp::handleMp1Message(int connId)
{
    HttpMessageStatus *msgStatus = (HttpMessageStatus*) mp1Socket_->getUserData();
    mp1HttpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();

    EV << "RavensAgentApp::handleMp1Message - payload: " << mp1HttpMessage->getBody() << endl;

    try
    {
        nlohmann::json jsonBody = nlohmann::json::parse(mp1HttpMessage->getBody()); // get the JSON structure
        if(!jsonBody.empty())
        {
            // find which service from the list is running at my Local Host
            int target = 0;
            int i = 0;
            //std::cout << jsonBody << std::endl;
            // jsonBody is a list of json objects. Lets run through it

            while(i < jsonBody.size())
            {
                if(jsonBody[i]["isLocal"] == "TRUE")
                {
                    EV << "The choosen one was: " << jsonBody[i]["transportInfo"]["endPoint"]["addresses"] << std::endl;
                    target = i;
                }
                i++;
            }
            jsonBody = jsonBody[target];
            //std::cout << "The choosen one was: " << jsonBody["transportInfo"]["endPoint"]["addresses"] << std::endl;
            std::string serName = jsonBody["serName"];
            if(serName.compare("LocationService") == 0)
            {
                if(jsonBody.contains("transportInfo"))
                {
                    nlohmann::json endPoint = jsonBody["transportInfo"]["endPoint"]["addresses"];
                    EV << "address: " << endPoint["host"] << " port: " <<  endPoint["port"] << endl;
                    std::string address = endPoint["host"];
                    serviceAddress = L3AddressResolver().resolve(address.c_str());
                    servicePort = endPoint["port"];
                    lsSocket_ = addNewSocket();
                    cMessage *m = new cMessage("connectLS");
                    scheduleAt(simTime()+0, m);
                }
            }
        	else if (serName.compare("RNIService") == 0)
        	{
                // RAVENS V3 - Using RNIS besides LS
                // Store RNIS connection details in dedicated variables to avoid race condition with Location Service
        		if(jsonBody.contains("transportInfo"))
        		{
        			nlohmann::json endPoint = jsonBody["transportInfo"]["endPoint"]["addresses"];
        			EV << "address: " << endPoint["host"] << " port: " <<  endPoint["port"] << endl;
        			std::string address = endPoint["host"];
        			rnisAddress = L3AddressResolver().resolve(address.c_str());
        			rnisPort = endPoint["port"];
        			rnisSocket_ = addNewSocket();
        			cMessage *m = new cMessage("connectRNIService");
        			scheduleAt(simTime()+0, m);
        		}
        	}
        	else
            {
                EV << "RavensAgentApp::handleMp1Message - No service found"<< endl;
                serviceAddress = L3Address();
            }
        }

    }
    catch (nlohmann::detail::parse_error &e)
    {
        EV <<  e.what() << std::endl;
        // body is not correctly formatted in JSON, manage it
        return;
    }
}

/**
* Routes incoming HTTP messages to the appropriate handler based on socket origin.
* Dispatches to MP1, Location Service, or RNIS message handlers accordingly.
*/
void RavensAgentApp::handleHttpMessage(int connId)
{
    EV << "RavensAgentApp::handleHttpMessage - Http Message Received" <<  connId << endl;
    if (mp1Socket_ != nullptr && connId == mp1Socket_->getSocketId())
    {
        handleMp1Message(connId);
    }
    else if (lsSocket_ != nullptr && connId == lsSocket_->getSocketId())
    {
        handleLSMessage(connId);
    }
	else if (rnisSocket_ != nullptr && connId == rnisSocket_->getSocketId())
	{
		handleRNISMessage(connId);
	}
}

/**
* Handles self-scheduled messages that drive the agent's logic and periodic tasks.
*
* Connection handlers:
*   - "connectMp1": Establishes connection to the MEC Platform (MP1 interface)
*   - "connectLS": Connects to the Location Service for user/AP tracking
*   - "connectRNIService": Connects to the RNI Service for radio measurements
*   - "connectRC": Initiates connection to the RAVENS Controller and sends join request
*
* Data transmission handlers:
*   - "sendAPDetails": Transmits discovered access point list to the controller
*   - "sendUserListSub": Subscribes to user list notifications from Location Service
*   - "sendUserList": Sends periodic user info snapshots to the controller
*   - "sendL2MeasSub": Subscribes to Layer 2 measurement notifications from RNIS
*/
void RavensAgentApp::handleSelfMessage(cMessage *msg)
{
    if(strcmp(msg->getName(), "connectMp1") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        connect(mp1Socket_, mp1Address, mp1Port);
        delete msg;
    }
    else if(strcmp(msg->getName(), "connectLS") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        if(!serviceAddress.isUnspecified() && lsSocket_->getState() != inet::TcpSocket::CONNECTED)
        {
            connect(lsSocket_, serviceAddress, servicePort);
        }
        else
        {
            if(serviceAddress.isUnspecified())
                EV << "RavensAgentApp::handleSelfMessage - Location service IP address is  unspecified (maybe response from the service registry is arriving)" << endl;
            else if(lsSocket_->getState() == inet::TcpSocket::CONNECTED)
                EV << "RavensAgentApp::handleSelfMessage - Location service socket is already connected" << endl;
        }
        delete msg;
    }
	// RAVENS V3
    else if(strcmp(msg->getName(), "connectRNIService") == 0)
    {
    	EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
    	if(!rnisAddress.isUnspecified() && rnisSocket_->getState() != inet::TcpSocket::CONNECTED)
    	{
    		connect(rnisSocket_, rnisAddress, rnisPort);
    	}
    	else
    	{
    		if(rnisAddress.isUnspecified())
    			EV << "RavensAgentApp::handleSelfMessage - RNI service IP address is  unspecified (maybe response from the service registry is arriving)" << endl;
    		else if(rnisSocket_->getState() == inet::TcpSocket::CONNECTED)
    			EV << "RavensAgentApp::handleSelfMessage - RNI service socket is already connected" << endl;
    	}
    	delete msg;
    }
    else if(strcmp(msg->getName(), "connectRC") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        connectToRavensController();
        // sendJoinNetworkRequest() is called from socketEstablished(TcpSocket*) once TCP is up
        delete msg;
    }
    else if(strcmp(msg->getName(), "sendAPDetails") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        // send AP information to RavensController and get the information about the type of information we want to get from the location service and with which frequency
        sendAPList();
        delete msg;
    }
    else if(strcmp(msg->getName(), "sendUserListSub") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        sendUsersListSubscription();
        delete msg;
    }
    else if(strcmp(msg->getName(), "sendUserList") == 0)
    {
        EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
        // Reschedule first, unconditionally: the timer must not die on an interval
        // where there happened to be nothing to send.
        cMessage *next = new cMessage("sendUserList");
        scheduleAt(simTime() + telemetryInterval_, next);
        // Event frames no longer ride this timer — they are sent from
        // handleLSMessage the moment a state change is confirmed. Only the
        // telemetry frame is still on a fixed cadence.
        sendDataFrame();
        delete msg;
    }
    else if(strcmp(msg->getName(), "sendL2MeasSub") == 0)
    {
    	EV << "RavensAgentApp::handleMessage- " << msg->getName() << endl;
    	sendL2MeasSubscription();
    	delete msg;
    }
    else
    {
        EV << "RavensAgentApp::handleMessage - Unrecognized message: " << msg->getName() << endl;
        delete msg;
    }
}

/**
* Establishes a UDP socket connection to the RAVENS Controller.
* Resolves the controller module path and address from configuration parameters.
* If the controller is not yet available, schedules a retry after 50ms.
*/
void RavensAgentApp::connectToRavensController()
{
    cMessage *msg = new cMessage("connectRC");

    std::string controller = par("controllerAddress").stringValue();
    cModule* controllerModule = findModuleByPath(par("controllerAddress").stringValue());

    EV << "RAVENS CONTROLLER ADDRESS: " << controller.c_str() << endl;

    if (controllerModule == nullptr) {
        EV << "RavensAgentApp::connectToRavensController - " << controller << " not found" << endl;
        scheduleAt(simTime() + 0.05, msg);
        EV << "RavensAgentApp::connectToRavensController - the node will retry to connect to the controller in 0.05 seconds " << endl;
    }
    else {
        delete msg;
        controllerAddress_ = L3AddressResolver().resolve(par("controllerAddress"));

        // UDP telemetry socket — periodic TELEMETRY_FRAME snapshots (loss-tolerant)
        controllerSocket_.setOutputGate(gate("socketOut"));
        controllerSocket_.bind(localPort_);
        controllerSocket_.setCallback(this);
        controllerSocket_.connect(controllerAddress_, controllerPort);
        EV << "RavensAgentApp::connectToRavensController - UDP data socket connected to " << controllerAddress_ << ":" << controllerPort << endl;

        // TCP signaling socket — config handshake (JOIN / INFRASTRUCTURE_DETAILS),
        // then kept open for EVENT_FRAMEs
        controllerMgmtSocket_.setOutputGate(gate("socketOut"));
        controllerMgmtSocket_.setCallback(this);
        controllerMgmtSocket_.connect(controllerAddress_, controllerMgmtPort_);
        EV << "RavensAgentApp::connectToRavensController - TCP mgmt socket connecting to " << controllerAddress_ << ":" << controllerMgmtPort_ << endl;
    }
}

/**
 * Processes HTTP responses from the Radio Network Information Service (RNIS).
 *
 * Handles two types of responses:
 * - Code 201: Confirms successful subscription creation to Layer 2 measurements
 * - Code 200: Processes subscription notifications containing radio metrics
 *
 * A notification (code 200) produces two things:
 * 1. One cell record, built fresh here and appended to cellSampleBuffer_ for the
 *    next telemetry frame. It holds the cell-level metrics the RNIS reports
 *    directly (PRB usage, PDR, active UE count) and the delay and data-volume
 *    aggregates computed here from the notification's per-UE section.
 * 2. Per-UE radio metrics attached to the users map — delay, PDR, data volume
 *    and RSRP — which travel with that UE's next Location Service sample.
 *
 * Only UEs already in the users map get their radio metrics attached; the map is
 * populated by the Location Service, and a UE the RNIS reports first is skipped.
 * The cell aggregates cover the whole per-UE section either way, so they span a
 * slightly wider population than the per-UE samples do — which is the reason
 * they are worth carrying rather than being recomputed downstream.
 */
void RavensAgentApp::handleRNISMessage(int connId)
{
	EV << mecHostId << " - RavensAgentApp::handleRNISMessage - RNIS Message Received - Socket ID: " << connId << endl;
	HttpMessageStatus *msgStatus = (HttpMessageStatus*) rnisSocket_->getUserData();
	serviceHttpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();
	HttpResponseMessage *rspMsg = dynamic_cast<HttpResponseMessage*>(serviceHttpMessage);

    if (rspMsg == nullptr) {
        EV << mecHostId << " - RavensAgentApp::handleRNISMessage - Error: Received message is not a valid HttpResponseMessage" << endl;
        return;
    }

	int code = rspMsg->getCode();
	EV << mecHostId << " - RavensAgentApp::handleRNISMessage - RNIS Message payload with code " << code << " received with body: " << rspMsg->getBody() << endl;

	if (code == 200)
	{
	    //std::cout << mecHostId << " - RNIS 200 response received" << std::endl;
		nlohmann::json jsonBody = nlohmann::json::parse(serviceHttpMessage->getBody());
		if (jsonBody.contains("subscriptionNotification")) {
			nlohmann::json notification = jsonBody["subscriptionNotification"];

			// A fresh record for this notification, timestamped once, here.
			//
			// Everything below writes into this record and nothing else. That is
			// the whole reason a value can no longer outlive the reading it came
			// from: a field this notification does not mention keeps the "not
			// measured" default it was constructed with, because there is no
			// earlier reading in this object to leave behind. It used to be one
			// long-lived record that every notification wrote into, so a cell
			// with no users kept reporting the delay of whoever passed through
			// last, and the timestamp said when the record was touched rather
			// than when each field in it was measured.
			AccessPointRadioInfoData cellSample;
			cellSample.setTimestamp(simTime());

			// Update AP-level stats
			if (notification.contains("cellInfo")) {
				nlohmann::json cellInfo = notification["cellInfo"];
				if (cellInfo.contains("ecgi")) {
					std::string cellId = std::to_string(cellInfo["ecgi"]["cellId"].get<int>());
					cellSample.setAccessPointId(cellId);
					// -1 = not measured (the RNIS omits fields whose collector has no data)
					cellSample.setDlTotalPrbUsageCell(cellInfo.value("dl_total_prb_usage_cell", -1.0));
					cellSample.setUlTotalPrbUsageCell(cellInfo.value("ul_total_prb_usage_cell", -1.0));
					cellSample.setDlNongbrPdrCell(cellInfo.value("dl_nongbr_pdr_cell", -1.0));
					cellSample.setUlNongbrPdrCell(cellInfo.value("ul_nongbr_pdr_cell", -1.0));
					cellSample.setNumberOfActiveUeDlNongbrCell(
					    cellInfo.value("number_of_active_ue_dl_nongbr_cell", -1));
				}
			}

			// Aggregate per-UE delay and data volume into cell-level stats.
			// Per-user RNIS fields are also attached to the users map here, which
			// is why this loop does two jobs at once.
			if (notification.contains("cellUEInfo")) {
			    std::vector<nlohmann::json> ueList;
			    if (notification["cellUEInfo"].is_array()) {
			        for (auto& ue : notification["cellUEInfo"])
			            ueList.push_back(ue);
			    } else {
			        ueList.push_back(notification["cellUEInfo"]);
			    }

			    double sumDlDelay = 0.0, sumUlDelay = 0.0;
			    double sumDlVol = 0.0, sumUlVol = 0.0;
			    int delayCount = 0;
			    for (auto& ue : ueList) {
			        // Aggregate only reported values — an absent key means "not measured"
			        // and must not drag the average toward zero.
			        if (ue.contains("dl_nongbr_delay_ue") || ue.contains("ul_nongbr_delay_ue")) {
			            sumDlDelay += ue.value("dl_nongbr_delay_ue", 0.0);
			            sumUlDelay += ue.value("ul_nongbr_delay_ue", 0.0);
			            delayCount++;
			        }
			        sumDlVol   += ue.value("dl_nongbr_data_volume_ue", 0.0);
			        sumUlVol   += ue.value("ul_nongbr_data_volume_ue", 0.0);

			        if (!ue.contains("associatedId") || !ue["associatedId"].contains("value")) {
			            EV << mecHostId << " - RavensAgentApp::handleRNISMessage - cellUEInfo without associatedId.value, skipping per-UE update" << endl;
			            continue;
			        }

			        // The users map is keyed by the LS address format ("acr:<ip>");
			        // RNIS reports the bare IP, so normalize before lookup.
			        std::string ueAddress = "acr:" + ue["associatedId"]["value"].get<std::string>();
			        auto userIt = users.find(ueAddress);
			        if (userIt == users.end()) {
			            EV << mecHostId << " - RavensAgentApp::handleRNISMessage - RNIS data for UE "
			               << ueAddress << " arrived before LS state; skipping" << endl;
			            continue;
			        }

			        UserRadioInfoData radioInfo;
			        if (ue.contains("ecgi") && ue["ecgi"].contains("cellId"))
			            radioInfo.setAccessPointId(std::to_string(ue["ecgi"]["cellId"].get<int>()));
			        radioInfo.setTimestamp(simTime());
			        radioInfo.setDlNongbrDelayUe(ue.value("dl_nongbr_delay_ue", -1.0));
			        radioInfo.setUlNongbrDelayUe(ue.value("ul_nongbr_delay_ue", -1.0));
			        radioInfo.setDlNongbrPdrUe(ue.value("dl_nongbr_pdr_ue", -1.0));
			        radioInfo.setUlNongbrPdrUe(ue.value("ul_nongbr_pdr_ue", -1.0));
			        radioInfo.setDlNongbrDataVolumeUe(ue.value("dl_nongbr_data_volume_ue", -1.0));
			        radioInfo.setUlNongbrDataVolumeUe(ue.value("ul_nongbr_data_volume_ue", -1.0));
			        radioInfo.setRsrp(ue.value("rsrp", -1.0));
			        userIt->second.setRadioInfo(radioInfo);
			    }
			    // A mean needs somebody to average over. With users present but
			    // none of them reporting a delay, there is no value to report -
			    // which is a different statement from "the delay was zero".
			    if (delayCount > 0) {
			        cellSample.setAvgDlDelay(sumDlDelay / delayCount);
			        cellSample.setAvgUlDelay(sumUlDelay / delayCount);
			    }
			    cellSample.setTotalDlDataVolume(sumDlVol);
			    cellSample.setTotalUlDataVolume(sumUlVol);
			    EV << mecHostId << " - RavensAgentApp::handleRNISMessage - aggregated " << ueList.size() << " UEs into cell stats" << endl;
			}
			else {
			    // No per-UE section at all means no users in this cell. A sum over
			    // nobody is genuinely zero, so the volumes are written as zero; a
			    // mean over nobody is undefined, so the delays stay at -1.
			    cellSample.setTotalDlDataVolume(0.0);
			    cellSample.setTotalUlDataVolume(0.0);
			    EV << mecHostId << " - RavensAgentApp::handleRNISMessage - no users in this cell" << endl;
			}

			// Buffered only in full mode, like the user samples: nothing drains
			// this in event-only mode, so it would grow for the whole run.
			if (agentMode_ == FULL_MODE)
			    cellSampleBuffer_.push_back(cellSample);
		}
	}

    else if (code == 201)
    {
        EV << mecHostId << " - RNIS SUBSCRIPTION CREATED!" << std::endl;
    }
    else
    {
        EV << "ERROR when getting info from RNIS" << std::endl;
    }
}

/**
 * Handles HTTP responses from the Location Service (LS).
 *
 * The LS sends the full list of currently attached UEs on every notification
 * (replacement semantics — absence from the list means the UE is gone).
 * This function runs at 1s granularity (LS subscription frequency) and is
 * the sole source of truth for user presence at this MEH.
 *
 * Three response types:
 *
 * code 200 / cellList:
 *   One-time response to the initial AP list query. Populates accessPoints
 *   and apIndex_, then triggers AP details transmission to the Controller.
 *
 * code 200 / subscriptionNotification:
 *   Periodic UE list from the LS subscription. Runs the stability-threshold
 *   diff logic, updating eventState_ (per-UE consecutive present/absent
 *   counters, see the struct definition in RavensAgentApp.h) and the users
 *   map together:
 *   1. Upsert loop — for each UE reported present this tick: updates
 *      location in the users map; in eventState_, cancels any in-progress
 *      absent streak (a return before EXIT confirmed) and extends the
 *      present streak. A present streak that reaches entryConfirmSamples_
 *      and has not yet been reported produces an ENTRY event immediately —
 *      not on the next frame tick — and marks the UE reported.
 *   2. Departure detection — walks eventState_, not the users map, because a
 *      UE leaves the users map on its first absent sample while its absent
 *      streak has to keep growing across ticks. Anyone being tracked but
 *      missing from this notification has its absent streak extended (started
 *      fresh if this is the first absent sample) and is removed from the
 *      users map, so the cell averages stop counting it right away. If the UE
 *      was reported and the absent streak reaches exitConfirmSamples_, an EXIT
 *      event is produced and the UE's eventState_ entry is erased — tracking
 *      is done. If the UE was never reported, its eventState_ entry is erased
 *      immediately with no event: the Controller was never told it existed, so
 *      there is nothing to retract, and a later reappearance starts a fresh
 *      streak. Buffered telemetry samples are untouched either way and still
 *      go out with the next frame.
 *   All events produced by this tick are batched into a single call to
 *   sendEventFrame(), which is invoked once per tick regardless of whether
 *   this tick produced anything, so a previously unsent event gets a chance
 *   to retry.
 *
 * code 201:
 *   Subscription confirmed. Schedules the first frame timer.
 */
void RavensAgentApp::handleLSMessage(int connId)
{
    EV << "RavensAgentApp::handleLSMessage - LS Message Received - Socket ID: " << connId << endl;

    HttpMessageStatus *msgStatus = (HttpMessageStatus*) lsSocket_->getUserData();
    serviceHttpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();
    HttpResponseMessage *rspMsg = dynamic_cast<HttpResponseMessage*>(serviceHttpMessage);

    if (rspMsg == nullptr)
    {
        EV << "RavensAgentApp::handleLSMessage - Error: received message is not a valid HttpResponseMessage" << endl;
        return;
    }

    int code = rspMsg->getCode();

    EV << "RavensAgentApp::handleLSMessage - LS Message payload with code " << code << " received: " <<  serviceHttpMessage->getBody() << endl;

    if(code == 200)
    {
        nlohmann::json jsonBody = nlohmann::json::parse(serviceHttpMessage->getBody());
        if(!jsonBody.empty())
        {
            // --- AP list (one-time, on initial query) ---
            if(jsonBody.contains("cellList"))
            {
                nlohmann::json cellList = jsonBody["cellList"];
                for (auto& cell : cellList)
                {
                    std::string cellId = to_string(cell["cellId"]);
                    long x = cell["position"]["x"];
                    long y = cell["position"]["y"];
                    NodeLocation apLocation = NodeLocation(x, y, 0);
                    AccessPointData apData = AccessPointData(cellId, apLocation);
                    accessPoints.push_back(apData);
                    apIndex_[cellId] = accessPoints.size() - 1;
                }
                cMessage *msg = new cMessage("sendAPDetails");
                scheduleAt(simTime() + 0.5, msg);
            }
            // --- Periodic UE list from subscription ---
            else if(jsonBody.contains("subscriptionNotification"))
            {
                nlohmann::json userInfoList = jsonBody["subscriptionNotification"]["userInfoList"];

                // Events confirmed at this tick — entries crossing entryConfirmSamples_,
                // exits crossing exitConfirmSamples_. Sent as one frame at the end.
                RavensEventList confirmedEvents;

                // Build current address set while upserting — single pass over userInfoList.
                // currentLSAddrs is used after the loop for departure detection.
                std::unordered_set<std::string> currentLSAddrs;
                for (auto& user : userInfoList)
                {
                    std::string address = user["userInfo"]["address"];
                    currentLSAddrs.insert(address);

                    std::string accessPointId = to_string(user["userInfo"]["accessPointId"]);
                    AccessPointData apData;
                    auto apIt = apIndex_.find(accessPointId);
                    if (apIt != apIndex_.end()) {
                        apData = accessPoints[apIt->second];
                    }

                    long x       = user["userInfo"]["locationInfo"]["x"];
                    long y       = user["userInfo"]["locationInfo"]["y"];
                    long z       = user["userInfo"]["locationInfo"]["z"];
                    long bearing = user["userInfo"]["locationInfo"]["velocity"]["bearing"].is_null() ? 0 : user["userInfo"]["locationInfo"]["velocity"]["bearing"].get<long>();
                    long speed   = user["userInfo"]["locationInfo"]["velocity"]["horizontalSpeed"];
                    UserLocation userLocation = UserLocation(x, y, z, bearing, speed);

                    auto it = users.find(address);
                    if (it != users.end())
                    {
                        // Existing user — update location state, preserve identity
                        it->second.setAccessPointId(apData.getAccessPointId());
                        it->second.setCurrentLocation(userLocation);
                        double newDistance = it->second.calculateDistanceToAP(
                            apData.getAccessPointLocation().getX(),
                            apData.getAccessPointLocation().getY(),
                            userLocation.getX(),
                            userLocation.getY()
                        );
                        it->second.setDistanceToAP(newDistance);
                        it->second.setTimestamp(simTime());
                    }
                    else
                    {
                        // New user — insert into the users map (current placement).
                        UserData userData = UserData(address, apData, userLocation);
                        userData.setTimestamp(simTime());
                        users[address] = userData;
                    }

                    // Record this tick as an observation of this UE. The sample is a
                    // copy taken while the value is current — position from this tick,
                    // radio values as the RNIS last reported them, each carrying its
                    // own timestamp so a repeated radio reading stays recognisable as
                    // a repeat.
                    //
                    // Because it is a copy, it no longer depends on the users map
                    // entry: when this UE later disappears and is erased below, the
                    // observations it already produced are safe here and still go out.
                    //
                    // Skipped in event-only mode, where no telemetry frame will ever
                    // be sent — that mode exists so a host does no telemetry work at
                    // all, and buffering samples nothing will drain would undo it.
                    if (agentMode_ == FULL_MODE)
                        sampleBuffer_[address].push_back(users[address]);

                    // eventState_ tracks stability independently of the users map, so
                    // it is updated the same way whether the UE already existed or not.
                    UeEventState& st = eventState_[address];
                    if (st.consecutiveAbsent > 0)
                    {
                        // Returned before its absent streak reached exitConfirmSamples_ —
                        // cancel the streak. If already reported, this is the whole story:
                        // the Controller's view never changed, so nothing is sent. If not
                        // yet reported, this cannot happen — an unreported UE's absent
                        // streak is erased outright on departure (see below), so a return
                        // always starts a brand-new entry, never reaches this branch.
                        EV << mecHostId << " - RavensAgentApp::handleLSMessage - User reappeared before EXIT was confirmed: "
                           << address << endl;
                        st.consecutiveAbsent = 0;
                        st.consecutivePresent = 1;
                    }
                    else if (st.consecutivePresent == 0)
                    {
                        // First sample ever seen for this address (or first sample after
                        // a fresh eventState_ entry was created above by operator[]).
                        st.firstDetectedAt = simTime();
                        st.consecutivePresent = 1;
                    }
                    else
                    {
                        st.consecutivePresent++;
                    }

                    if (!st.reported && st.consecutivePresent >= entryConfirmSamples_)
                    {
                        RavensEvent e;
                        e.ueAddress = address;
                        e.eventType = EVENT_ENTRY;
                        e.samplesSinceChange = st.consecutivePresent;
                        e.firstDetectedAt = st.firstDetectedAt;
                        confirmedEvents.push_back(e);
                        st.reported = true;
                        EV << mecHostId << " - RavensAgentApp::handleLSMessage - ENTRY confirmed for " << address
                           << " after " << st.consecutivePresent << " sample(s)" << endl;
                    }
                }

                // Departure detection — the Location Service replaces its whole list
                // every tick, so anyone being tracked who is missing from this tick's
                // list was not observed this second.
                //
                // This walks eventState_, not the users map. The users map is "who is
                // here now", and a UE is dropped from it the moment it stops being
                // reported — so counting absent samples there can only ever reach one,
                // and a threshold of two is never crossed. Absence is a property of the
                // tracking state, which is meant to outlive presence; that is what it
                // is for.
                for (auto it = eventState_.begin(); it != eventState_.end(); )
                {
                    if (currentLSAddrs.find(it->first) != currentLSAddrs.end())
                    {
                        ++it;      // observed this tick — already handled in the loop above
                        continue;
                    }

                    UeEventState& st = it->second;
                    if (st.consecutiveAbsent == 0)
                        st.firstDetectedAt = simTime();   // first absent sample of this streak
                    st.consecutiveAbsent++;
                    st.consecutivePresent = 0;

                    // Not here as far as anything about the present is concerned — the
                    // cell averages must not include it. A no-op once the streak is
                    // under way.
                    //
                    // sampleBuffer_ is deliberately left alone: this UE's observations
                    // from earlier in the interval are still owed to the Controller and
                    // leave with the next frame. That buffer is emptied when a frame is
                    // sent, and at no other time.
                    users.erase(it->first);

                    if (!st.reported)
                    {
                        // The Controller was never told this UE existed, so there is
                        // nothing to retract. Drop the state rather than keep counting
                        // an absence nobody needs to hear about; a later reappearance
                        // starts as a brand-new UE.
                        EV << mecHostId << " - RavensAgentApp::handleLSMessage - Unreported user left, no event: "
                           << it->first << endl;
                        it = eventState_.erase(it);
                        continue;
                    }

                    if (st.consecutiveAbsent >= exitConfirmSamples_)
                    {
                        RavensEvent e;
                        e.ueAddress = it->first;
                        e.eventType = EVENT_EXIT;
                        e.samplesSinceChange = st.consecutiveAbsent;
                        e.firstDetectedAt = st.firstDetectedAt;
                        confirmedEvents.push_back(e);
                        EV << mecHostId << " - RavensAgentApp::handleLSMessage - EXIT confirmed for "
                           << it->first << " after " << st.consecutiveAbsent << " sample(s)" << endl;
                        it = eventState_.erase(it);   // done — reported and retracted
                        continue;
                    }

                    ++it;   // still counting — the UE stays tracked, so the next tick
                            // continues the streak instead of losing it
                }

                // Sent every tick, whether or not confirmedEvents is empty, so a
                // previously unsent frame (channel was down) gets a chance to retry.
                sendEventFrame(confirmedEvents);
            }
        }
        else
        {
            EV << "RavensAgentApp::handleLSMessage - LS Message without valid body!" << endl;
        }
    }
    else if(code == 201)
    {
        // it means that we sucefully subscribed to the users/list uri and we can start sending user list messages to the controller
        cMessage *msg = new cMessage("sendUserList");
        scheduleAt(simTime() + 1, msg);
    }
    else
    {
        EV << "RavensAgentApp::handleLSMessage - LS Message payload with code " << code << " received: " <<  serviceHttpMessage->getBody() << endl;
    }

}

/**
* Processes incoming messages from the RAVENS Controller socket.
* Handles JOIN_NETWORK_ACK by initiating MP1 connection for service discovery.
* Handles INFRASTRUCTURE_DETAILS_ACK by extracting the retrieval rate and
* scheduling the user list subscription. Delegates other messages to MecAppBase.
*/
void RavensAgentApp::handleProcessedMessage(cMessage *msg)
{
    EV << "RavensAgentApp::handleProcessedMessage - Message Received " << msg->getName() << endl;
    if (controllerMgmtSocket_.belongsToSocket(msg)) {
        controllerMgmtSocket_.processMessage(msg);
    } else {
        MecAppBase::handleProcessedMessage(msg);
    }
}

/** Sends a POST request to subscribe to user list notifications from the Location Service. */
void RavensAgentApp::sendUsersListSubscription()
{
    EV << "RavensAgentApp::sendUsersListSubscription - Sending users/list Subscription" << endl;
    std::string body = "{  \"usersListNotificationSubscription\": {"
                           "\"callbackReference\" : {"
                            "\"callbackData\":\"v0\","
                            "\"notifyURL\":\"ravens.user.list\"},"
                           "\"checkImmediate\": \"true\","
                            "\"frequency\": 1,"
                            "\"cells\": [0]"
                            "}"
                            "}\r\n";
    std::string uri = "/example/location/v2/subscriptions/users/list";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());

    Http::sendPostRequest(lsSocket_, body.c_str(), host.c_str(), uri.c_str());
}

/** Sends a POST request to subscribe to user density notifications from the Location Service. */
void RavensAgentApp::sendUsersDensitySubscription()
{
    EV << "RavensAgentApp::sendUsersDensitySubscription - Sending users/density Subscription" << endl;
    std::string body = "{  \"usersDensityNotificationSubscription\": {"
                           "\"callbackReference\" : {"
                            "\"callbackData\":\"v0\","
                            "\"notifyURL\":\"ravens.user.density\"},"
                           "\"checkImmediate\": \"false\","
                            "\"frequency\": 5,"
                            "\"cells\": [0]"
                            "}"
                            "}\r\n";
    std::string uri = "/example/location/v2/subscriptions/users/density";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());

    Http::sendPostRequest(lsSocket_, body.c_str(), host.c_str(), uri.c_str());
}

/** Sends a POST request to subscribe to Layer 2 measurement notifications from RNIS. */
void RavensAgentApp::sendL2MeasSubscription()
{
    EV << "RavensAgentApp::sendRNISSubscription - Sending RNIS L2 Measurement Subscription" << endl;

    std::string body =
        "{ \"L2MeasurementSubscription\": {"
            "\"callbackReference\": {"
                "\"callbackData\": \"v0\","
                "\"notifyURL\": \"ravens.rnis.layer2\"},"
            "\"cells\": [0],"
            "\"checkImmediate\": \"true\","  // Get data immediately after subscription
            "\"frequency\": 1"  // Notification frequency in seconds
        "}"
        "}\r\n";

    std::string uri = "/example/rni/v2/subscriptions/layer2_meas";
    std::string host = rnisSocket_->getRemoteAddress().str() + ":" +
                       std::to_string(rnisSocket_->getRemotePort());

    Http::sendPostRequest(rnisSocket_, body.c_str(), host.c_str(), uri.c_str());
}

/** Sends a GET request to query Layer 2 measurements from the RNIS. */
void RavensAgentApp::sendRNISRequest()
{
    const char *users_uri = "/example/rni/v2/queries/layer2_meas";
    std::string host = rnisSocket_->getRemoteAddress().str()+":"+std::to_string(rnisSocket_->getRemotePort());
    Http::sendGetRequest(rnisSocket_, host.c_str(), users_uri);
    EV << mecHostId << " - RavensAgentApp::sendUserListRequest - uri " << users_uri << " to host " << host.c_str() << endl;
}

/** Sends a GET request to query the list of connected users from the Location Service. */
void RavensAgentApp::sendUserListRequest()
{
    const char *users_uri = "/example/location/v2/queries/users";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());
    Http::sendGetRequest(lsSocket_, host.c_str(), users_uri);
    EV << "RavensAgentApp::sendUserListRequest - uri " << users_uri << " to host " << host.c_str() << endl;
    return;
}

/** Sends a GET request to query the list of access points from the Location Service. */
void RavensAgentApp::sendAPListRequest()
{
    const char *zones_uri = "/example/location/v2/queries/accessPoints";
    std::string host = lsSocket_->getRemoteAddress().str()+":"+std::to_string(lsSocket_->getRemotePort());
    Http::sendGetRequest(lsSocket_, host.c_str(), zones_uri);
    EV << "RavensAgentApp::sendAPListRequest - uri " << zones_uri << " to host " << host.c_str() << endl;
    return;
}

void RavensAgentApp::handleServiceMessage(int connId)
{
    EV << "RavensAgentApp::handleServiceMessage - Service Message Received" <<  connId << endl;
}

void RavensAgentApp::handleUeMessage(cMessage *msg)
{
    EV << "Message from UE" << endl;
}

void RavensAgentApp::socketDataArrived(UdpSocket *socket, inet::Packet *packet){
    EV << "RavensAgentApp::socketDataArrived - socketDataArrived FROM RavensControllerApp" << endl;
}

void RavensAgentApp::socketErrorArrived(UdpSocket *socket, inet::Indication *indication) {
    EV << "RavensAgentApp::socketErrorArrived - socketErrorArrived" << endl;
}

void RavensAgentApp::socketClosed(UdpSocket *socket){
    EV << "RavensAgentApp::socketClosed - socketClosed" << endl;
}

// --- TcpSocket::ICallback for controllerMgmtSocket_ ---

void RavensAgentApp::socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *availableInfo){
    MecAppBase::socketAvailable(socket, availableInfo);
}

void RavensAgentApp::socketEstablished(inet::TcpSocket *socket){
    if (socket == &controllerMgmtSocket_) {
        EV << "RavensAgentApp::socketEstablished - controller mgmt TCP connected, sending JOIN" << endl;
        sendJoinNetworkRequest();
        return;
    }
    MecAppBase::socketEstablished(socket);
}

void RavensAgentApp::socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent){
    if (socket == &controllerMgmtSocket_) {
        auto received = packet->peekAtFront<RavensLinkPacket>();
        if (received->getType() == JOIN_NETWORK_ACK) {
            EV << "RavensAgentApp::socketDataArrived(TCP) - JOIN_NETWORK_ACK received" << endl;
            delete packet;
            scheduleAt(simTime(), new cMessage("connectMp1"));
        } else if (received->getType() == INFRASTRUCTURE_DETAILS_ACK) {
            auto ack = packet->peekAtFront<RavensLinkInfrastructureDetailsMessageAck>();
            telemetryInterval_ = ack->getTelemetryIntervalMs() / 1000.0;
            agentMode_ = ack->getAgentMode();
            EV << "RavensAgentApp::socketDataArrived(TCP) - INFRASTRUCTURE_DETAILS_ACK: telemetryInterval="
               << telemetryInterval_ << "s, agentMode=" << agentMode_ << endl;
            delete packet;
            // handshake complete — connection stays open as the persistent
            // signaling channel for EVENT_FRAMEs
            scheduleAt(simTime(), new cMessage("sendUserListSub"));
        } else {
            EV << "RavensAgentApp::socketDataArrived(TCP) - unexpected message type, dropping" << endl;
            delete packet;
        }
        return;
    }
    MecAppBase::socketDataArrived(socket, packet, urgent);
}

void RavensAgentApp::socketPeerClosed(inet::TcpSocket *socket){
    if (socket == &controllerMgmtSocket_) {
        EV << "RavensAgentApp::socketPeerClosed - controller closed mgmt connection" << endl;
        controllerMgmtSocket_.close();
        return;
    }
    MecAppBase::socketPeerClosed(socket);
}

void RavensAgentApp::socketClosed(inet::TcpSocket* socket)
{
    if (socket == &controllerMgmtSocket_) {
        EV << "RavensAgentApp::socketClosed - controller mgmt TCP connection closed" << endl;
        return;
    }

    std::string socketType = "UNKNOWN";
    if (socket == rnisSocket_)
        socketType = "RNIS";
    else if (socket == lsSocket_)
        socketType = "LS";
    else if (socket == mp1Socket_)
        socketType = "MP1";

    EV_WARN << "[" << simTime() << "] " << mecHostId
            << " - TCP SOCKET CLOSED! Socket ID: " << socket->getSocketId()
            << " (" << socketType << " SOCKET)" << endl;

    MecAppBase::socketClosed(socket);
}

void RavensAgentApp::socketFailure(inet::TcpSocket *socket, int code){
    if (socket == &controllerMgmtSocket_) {
        EV << "RavensAgentApp::socketFailure - controller mgmt TCP failure, code=" << code << endl;
        return;
    }
    MecAppBase::socketFailure(socket, code);
}

void RavensAgentApp::socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status){
    MecAppBase::socketStatusArrived(socket, status);
}

void RavensAgentApp::socketDeleted(inet::TcpSocket *socket){
    MecAppBase::socketDeleted(socket);
}


std::string RavensAgentApp::getMecHostId()
{
    return mecHostId;
}

}
