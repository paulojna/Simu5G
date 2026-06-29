#include "RavensAgentApp.h"

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
    this->accessPointRadioInformation = nullptr;
}

RavensAgentApp::~RavensAgentApp()
{
	delete accessPointRadioInformation;
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

    frameInterval_ = par("frameInterval");
    agentMode_ = AGENT_MODE_EVENT_AND_DATA; // default until ACK received

	accessPointRadioInformation = new AccessPointRadioInfoData();

    // connection to the RAVENS CONTROLLER
    auto *msg = new cMessage("connectRC");
    scheduleAt(simTime() + 0.5, msg);
}

void RavensAgentApp::finish()
{
    MecAppBase::finish();
    EV << "RavensAgentApp::finish()" << endl;

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
    request->setChunkLength(B(500));
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
    request->setChunkLength(B(500));
    request->setType(INFRAESTRUCTURE_DETAILS);
    request->setRequestId(0);
    request->setTimeStamp(simTime());
    request->setMecHostId(getMecHostId().c_str());
    request->setAPList(accessPoints);
    packet->insertAtBack(request);
    controllerMgmtSocket_.send(packet);
}

/**
 * Sends periodic snapshots of user information to the RAVENS Controller.
 *
 * Implements hybrid send strategy:
 * - Sends immediately when any user has fresh data from Location Service or RNIS (timestamp-based change detection)
 * - Forces periodic heartbeat updates even without changes to prevent controller timeout
 *
 * Before sending, purges stale users whose data hasn't been updated within the TTL window.
 * Includes both per-user data (location + radio stats) and AP-level radio information in each snapshot.
 */
void RavensAgentApp::sendEventFrame()
{
    if (pendingEntries_.empty() && pendingExits_.empty())
    {
        EV << mecHostId << " - RavensAgentApp::sendEventFrame - no changes, skipping" << endl;
        return;
    }

    RavensEventList events;
    for (const auto& [addr, ev] : pendingEntries_) {
        RavensEvent e;
        e.ueAddress = addr;
        e.eventType = EVENT_ENTRY;
        e.samplesSinceChange = ev.sampleCount;
        e.firstDetectedAt = ev.firstDetectedAt;
        events.push_back(e);
    }
    for (const auto& [addr, ev] : pendingExits_) {
        RavensEvent e;
        e.ueAddress = addr;
        e.eventType = EVENT_EXIT;
        e.samplesSinceChange = ev.sampleCount;
        e.firstDetectedAt = ev.firstDetectedAt;
        events.push_back(e);
    }

    inet::Packet* packet = new inet::Packet("RavensLinkEventMessage");
    auto chunk = inet::makeShared<RavensLinkEventMessage>();
    chunk->setChunkLength(inet::B(500));
    chunk->setType(UE_EVENT);
    chunk->setRequestId(localSnapshotCounter++);
    chunk->setTimeStamp(simTime());
    chunk->setMecHostId(getMecHostId().c_str());
    chunk->setEvents(events);
    packet->insertAtBack(chunk);
    controllerSocket_.send(packet);

    EV << mecHostId << " - RavensAgentApp::sendEventFrame - sent " << events.size() << " events" << endl;

    pendingEntries_.clear();
    pendingExits_.clear();
}

void RavensAgentApp::sendDataFrame()
{
    if (agentMode_ != AGENT_MODE_EVENT_AND_DATA)
        return;

    // Compute avg distance to AP from current LS user map
    if (accessPointRadioInformation != nullptr && !users.empty()) {
        double totalDist = 0.0;
        for (const auto& [addr, ud] : users)
            totalDist += ud.getDistanceToAP();
        accessPointRadioInformation->setAvgDistanceToAp(totalDist / users.size());
    }

    inet::Packet* packet = new inet::Packet("RavensLinkDataFrameMessage");
    auto chunk = inet::makeShared<RavensLinkDataFrameMessage>();
    chunk->setChunkLength(inet::B(500));
    chunk->setType(DATA_FRAME);
    chunk->setRequestId(localSnapshotCounter++);
    chunk->setTimeStamp(simTime());
    chunk->setMecHostId(getMecHostId().c_str());
    chunk->setUsers(users);
    if (accessPointRadioInformation != nullptr)
        chunk->setApRadioInfo(*accessPointRadioInformation);
    packet->insertAtBack(chunk);
    controllerSocket_.send(packet);

    EV << mecHostId << " - RavensAgentApp::sendDataFrame - sent data frame with " << users.size() << " users" << endl;
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
        // Reschedule first (unconditional — C4: timer must not die on a quiet interval)
        cMessage *next = new cMessage("sendUserList");
        scheduleAt(simTime() + frameInterval_, next);
        // Then conditionally send frames
        sendEventFrame();
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

        // UDP data socket — sends event frames and data frames
        controllerSocket_.setOutputGate(gate("socketOut"));
        controllerSocket_.bind(localPort_);
        controllerSocket_.setCallback(this);
        controllerSocket_.connect(controllerAddress_, controllerPort);
        EV << "RavensAgentApp::connectToRavensController - UDP data socket connected to " << controllerAddress_ << ":" << controllerPort << endl;

        // TCP mgmt socket — config handshake (JOIN / INFRAESTRUCTURE_DETAILS), close-after-ACK
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
 * When receiving notifications (code 200), extracts and updates:
 * 1. Cell-level radio statistics (AP-level): PRB usage and PDR metrics stored in accessPointRadioInformation
 * 2. Per-user radio statistics (UE-level): delay, PDR, and data volume metrics for each connected user
 *
 * Uses upsert pattern: only updates radio metrics for users already in the users map (created by Location Service).
 * Updates both rnisUpdate and lastUpdated timestamps to track data freshness.
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

			// Update AP-level stats
			if (notification.contains("cellInfo")) {
				nlohmann::json cellInfo = notification["cellInfo"];
				if (cellInfo.contains("ecgi")) {
					std::string cellId = std::to_string(cellInfo["ecgi"]["cellId"].get<int>());
					accessPointRadioInformation->setAccessPointId(cellId);
					accessPointRadioInformation->setDlTotalPrbUsageCell(cellInfo.value("dl_total_prb_usage_cell", 0.0));
					accessPointRadioInformation->setUlTotalPrbUsageCell(cellInfo.value("ul_total_prb_usage_cell", 0.0));
					accessPointRadioInformation->setDlNongbrPdrCell(cellInfo.value("dl_nongbr_pdr_cell", 0.0));
					accessPointRadioInformation->setUlNongbrPdrCell(cellInfo.value("ul_nongbr_pdr_cell", 0.0));
				}
			}

			// Aggregate per-UE delay and data volume into cell-level stats.
			// Per-user RNIS fields are not stored — cell aggregates go to accessPointRadioInformation.
			if (notification.contains("cellUEInfo") && accessPointRadioInformation != nullptr) {
			    std::vector<nlohmann::json> ueList;
			    if (notification["cellUEInfo"].is_array()) {
			        for (auto& ue : notification["cellUEInfo"])
			            ueList.push_back(ue);
			    } else {
			        ueList.push_back(notification["cellUEInfo"]);
			    }

			    double sumDlDelay = 0.0, sumUlDelay = 0.0;
			    double sumDlVol = 0.0, sumUlVol = 0.0;
			    int count = 0;
			    for (auto& ue : ueList) {
			        sumDlDelay += ue.value("dl_nongbr_delay_ue", 0.0);
			        sumUlDelay += ue.value("ul_nongbr_delay_ue", 0.0);
			        sumDlVol   += ue.value("dl_nongbr_data_volume_ue", 0.0);
			        sumUlVol   += ue.value("ul_nongbr_data_volume_ue", 0.0);
			        ++count;
			    }
			    if (count > 0) {
			        accessPointRadioInformation->setAvgDlDelay(sumDlDelay / count);
			        accessPointRadioInformation->setAvgUlDelay(sumUlDelay / count);
			    }
			    accessPointRadioInformation->setTotalDlDataVolume(sumDlVol);
			    accessPointRadioInformation->setTotalUlDataVolume(sumUlVol);
			    accessPointRadioInformation->setTimestamp(simTime());
			    EV << mecHostId << " - RavensAgentApp::handleRNISMessage - aggregated " << count << " UEs into cell stats" << endl;
			}
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
 *   Periodic UE list from the LS subscription. Runs the eRAVENS diff logic:
 *   1. Upsert loop — updates existing users, inserts new ones.
 *      New users are recorded in pendingEntries_ with firstDetectedAt and
 *      sampleCount, which accumulate across 1s intervals until the next
 *      control frame fires (every frameInterval_).
 *   2. Departure detection — any user in the local map absent from this
 *      notification is removed immediately and added to pendingExits_.
 *      pendingExits_ tracks firstDetectedAt and sampleCount so the Controller
 *      receives confidence context (e.g. absent for 3 samples = confident EXIT).
 *   Both pending maps are cleared by sendEventFrame() after each frame.
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
                        // New user — insert and track as pending ENTRY.
                        // timestamp serves as firstDetectedAt for the control frame.
                        UserData userData = UserData(address, apData, userLocation);
                        userData.setTimestamp(simTime());
                        users[address] = userData;

                        // If user reappeared after a pending EXIT, cancel the exit.
                        // sampleCount accumulates across LS intervals until frame fires.
                        pendingExits_.erase(address);
                        auto entryIt = pendingEntries_.find(address);
                        if (entryIt == pendingEntries_.end())
                            pendingEntries_[address] = {simTime(), 1};
                        else
                            entryIt->second.sampleCount++;

                        EV << "RavensAgentApp::handleLSMessage - New user detected: " << address << endl;
                    }
                }

                // Departure detection — LS has replacement semantics: any user
                // absent from this notification has left this cell. Remove from
                // users map immediately and accumulate in pendingExits_ so the
                // Controller receives sampleCount confidence on next control frame.
                for (auto it = users.begin(); it != users.end(); )
                {
                    if (currentLSAddrs.find(it->first) == currentLSAddrs.end())
                    {
                        // Cancel any pending ENTRY for this user (left before frame fired)
                        pendingEntries_.erase(it->first);
                        auto exitIt = pendingExits_.find(it->first);
                        if (exitIt == pendingExits_.end())
                            pendingExits_[it->first] = {simTime(), 1};
                        else
                            exitIt->second.sampleCount++;

                        EV << "RavensAgentApp::handleLSMessage - User departed: " << it->first
                           << " (absent for " << pendingExits_[it->first].sampleCount << " sample(s))" << endl;

                        it = users.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
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
* Handles INFRAESTRUCTURE_DETAILS_ACK by extracting the retrieval rate and
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
        } else if (received->getType() == INFRAESTRUCTURE_DETAILS_ACK) {
            auto ack = packet->peekAtFront<RavensLinkInfrastructureDetailsMessageAck>();
            frameInterval_ = ack->getRate() / 1000.0;
            agentMode_ = ack->getAgentMode();
            EV << "RavensAgentApp::socketDataArrived(TCP) - INFRAESTRUCTURE_DETAILS_ACK: frameInterval="
               << frameInterval_ << "s, agentMode=" << agentMode_ << endl;
            delete packet;
            controllerMgmtSocket_.close(); // handshake complete — close TCP connection
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
