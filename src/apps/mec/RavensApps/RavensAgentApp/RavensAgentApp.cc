#include "RavensAgentApp.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/Packet_m.h"

#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"

#include "nodes/mec/utils/httpUtils/httpUtils.h"
#include "nodes/mec/utils/httpUtils/json.hpp"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpRequestMessage/HttpRequestMessage.h"
#include "nodes/mec/MECPlatform/MECServices/packets/HttpResponseMessage/HttpResponseMessage.h"

#include <map>

namespace simu5g {

using namespace inet;
Define_Module(RavensAgentApp);

RavensAgentApp::RavensAgentApp(): MecAppBase()
{
    this->sendInterval = 1; // default value
    this->localSnapshotCounter = 0;
    this->accessPointRadioInformation = nullptr;
    this->userList = nullptr;
}

RavensAgentApp::~RavensAgentApp()
{
    cancelAndDelete(userList);
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

    controllerPort = par("controllerPort");
    localPort_ = par("localPort");
    ttl_ = par("ttl"); // Initialize TTL

    userList = new cMessage("userList");

    accessPoints = std::vector<AccessPointData>();
    users = std::unordered_map<std::string, UserData>();

    this->mecHostId = mecHost->getName();
	this->forceUpdateInterval_ = 5;
	this->lastSentTimestamp_ = simTime();
	this->hasPendingUpdates_ = false;

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
    controllerSocket_.send(packet);
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
    controllerSocket_.send(packet);
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
void RavensAgentApp::sendUsersInfoSnapshot()
{
    // Check if force update interval has been reached
    bool timeToForceUpdate = (simTime() - lastSentTimestamp_) >= forceUpdateInterval_;

    // PERFORMANCE IMPROVEMENT TEST: Using dirty flag instead of full user scan
    // Original code commented out for comparison:
    // bool hasRecentUpdate = false;
    // for (const auto& [address, user] : users) {
    //     if (user.getLsUpdate() > lastSentTimestamp_ ||
    //         user.getRnisUpdate() > lastSentTimestamp_) {
    //         hasRecentUpdate = true;
    //         break;
    //     }
    // }

    // Send if we have fresh data (dirty flag) OR force interval reached
    if (hasPendingUpdates_ || timeToForceUpdate)
    {
        // Purge stale users (TTL check)
        auto it = users.begin();
        while (it != users.end()) {
            if (simTime() - it->second.getLastUpdated() > ttl_) {
                EV << "RavensAgentApp::sendUsersInfoSnapshot - Purging stale user: " << it->first << endl;
                it = users.erase(it);
            } else {
                ++it;
            }
        }

        // Build and send the snapshot message
        EV << "RavensAgentApp::sendUsersInfoSnapshot - Sending User Info Snapshot (reason: "<< (hasPendingUpdates_ ? "fresh data" : "force update") << ")" << endl;
        inet::Packet* packet = new inet::Packet("RavensLinkUsersInfoSnapshotMessage");
        auto request = inet::makeShared<RavensLinkUsersInfoSnapshotMessage>();
        request->setChunkLength(B(500));
        request->setType(USERS_INFO_SNAPSHOT);
        request->setRequestId(localSnapshotCounter);
        request->setTimeStamp(simTime());
        request->setMecHostId(getMecHostId().c_str());
        request->setUsers(users);

        // Include AP-level radio information
        if (accessPointRadioInformation != nullptr)
        {
            request->setApRadioInfo(*accessPointRadioInformation);
        }
        else
        {
            EV << "RavensAgentApp::sendUsersInfoSnapshot - WARNING: accessPointRadioInformation is null" << endl;
        }

        packet->insertAtBack(request);
        controllerSocket_.send(packet);

        // Update state
        localSnapshotCounter++;
        lastSentTimestamp_ = simTime();
        hasPendingUpdates_ = false;  // Reset dirty flag after sending
    }
    else
    {
        EV << "RavensAgentApp::sendUsersInfoSnapshot - No fresh data and force interval not reached, skipping send" << endl;
    }

    // Schedule next check
    cMessage *msg = new cMessage("sendUserList");
    EV << "RavensAgentApp::sendUsersInfoSnapshot - Next check scheduled in " << getRetrievalInterval() << " seconds" << endl;
    scheduleAt(simTime() + getRetrievalInterval(), msg);
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
        sendJoinNetworkRequest();
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
        sendUsersInfoSnapshot();
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
        controllerSocket_.setOutputGate(gate("socketOut"));
        controllerSocket_.bind(localPort_);
        controllerSocket_.setCallback(this);

        controllerAddress_ = L3AddressResolver().resolve(par("controllerAddress")); // ravensController

        EV << "Connecting to " << controllerAddress_ << " port=" << controllerPort << endl;

        controllerSocket_.connect(controllerAddress_, controllerPort);
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

			// Update per-user stats
			if (notification.contains("cellUEInfo")) {
			    // Handle both array (multiple UEs) and single object (one UE) formats
			    std::vector<nlohmann::json> ueList;
			    if (notification["cellUEInfo"].is_array()) {
			        for (auto &ue : notification["cellUEInfo"]) {
			            ueList.push_back(ue);
			        }
			    } else {
			        // Single UE case - wrap in vector
			        ueList.push_back(notification["cellUEInfo"]);
			    }

			    //std::cout << mecHostId << "RNIS response contains " << ueList.size() << " UEs" << endl;
				for (auto &ue : ueList) {
					if (ue.contains("associatedId") && ue["associatedId"].contains("value")) {
						std::string address = "acr:" + ue["associatedId"]["value"].get<std::string>();
					    //std::cout << mecHostId << "  RNIS UE address: " << address << std::endl;

						auto it = users.find(address);
						if (it != users.end()) {
						    //std::cout << mecHostId << "    -> FOUND in users map, updating RNIS" << std::endl;
							// Update radio stats for existing user
							it->second.setDlNongbrDelayUe(ue.value("dl_nongbr_delay_ue", 0.0));
							it->second.setDlNongbrPdrUe(ue.value("dl_nongbr_pdr_ue", 0.0));
							it->second.setDlNongbrDataVolumeUe(ue.value("dl_nongbr_data_volume_ue", 0.0));
							it->second.setUlNongbrDelayUe(ue.value("ul_nongbr_delay_ue", 0.0));
							it->second.setUlNongbrPdrUe(ue.value("ul_nongbr_pdr_ue", 0.0));
							it->second.setUlNongbrDataVolumeUe(ue.value("ul_nongbr_data_volume_ue", 0.0));
							omnetpp::simtime_t dataTime = simTime();
							it->second.setRnisUpdate(dataTime);
							it->second.setLastUpdated(dataTime);
							hasPendingUpdates_ = true;  // Mark dirty for snapshot
						}
					    else
					    {
					        //std::cout << mecHostId << "    -> NOT FOUND in users map" << std::endl;
					    }
					}
				}
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
 * Processes HTTP responses from the Location Service.
 *
 * Handles three types of responses:
 * - Initial cellList (code 200): Discovers access points and their positions, triggers AP details transmission to controller
 * - Subscription notification (code 200): Receives periodic user location updates from subscribed cells
 * - Subscription confirmation (code 201): Confirms successful subscription, triggers periodic snapshot sending
 *
 * For user location notifications, implements upsert logic:
 * - Existing users: Updates location and access point while preserving RNIS radio statistics
 * - New users: Creates UserData entry with location, will be enriched with radio stats when RNIS updates arrive
 *
 * Updates both lsUpdate and lastUpdated timestamps to track when location data was last refreshed.
 */
void RavensAgentApp::handleLSMessage(int connId)
{
    EV << "RavensAgentApp::handleLSMessage - LS Message Received - Socket ID: " << connId << endl;

    HttpMessageStatus *msgStatus = (HttpMessageStatus*) lsSocket_->getUserData();
    serviceHttpMessage = (HttpBaseMessage*) msgStatus->httpMessageQueue.front();
    HttpResponseMessage *rspMsg = dynamic_cast<HttpResponseMessage*>(serviceHttpMessage);

    if (rspMsg == nullptr) {
        EV << "RavensAgentApp::handleLSMessage - Error: received message is not a valid HttpResponseMessage" << endl;
        return;
    }

    int code = rspMsg->getCode();

    EV << "RavensAgentApp::handleLSMessage - LS Message payload with code " << code << " received: " <<  serviceHttpMessage->getBody() << endl;

    if(code == 200)
    {
        // get the JSON structure
        nlohmann::json jsonBody = nlohmann::json::parse(serviceHttpMessage->getBody());
        if(!jsonBody.empty())
        {
            // find if the json contains the cellList and fill the accessPoints vector
            if(jsonBody.contains("cellList"))
            {
                nlohmann::json cellList = jsonBody["cellList"];
                //std::cout << "MecHostId" << getMecHostId() << std::endl;
                //std::cout << "cellList: " << jsonBody << std::endl;
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
                // send the information we were just given to the RavensController
                cMessage *msg = new cMessage("sendAPDetails");
                scheduleAt(simTime() + 0.5, msg);
            }
            else if(jsonBody.contains("subscriptionNotification"))
            {
                nlohmann::json userInfoList = jsonBody["subscriptionNotification"]["userInfoList"];
                for (auto& user : userInfoList)
                {
                    std::string address = user["userInfo"]["address"];
                    std::string accessPointId = to_string(user["userInfo"]["accessPointId"]);
                    // get accessPointData from index (O(1) lookup)
                    AccessPointData apData;
                    auto apIt = apIndex_.find(accessPointId);
                    if (apIt != apIndex_.end()) {
                        apData = accessPoints[apIt->second];
                    }
                    EV << "X" << endl;
                    long x = user["userInfo"]["locationInfo"]["x"];
                    long y = user["userInfo"]["locationInfo"]["y"];
                    long z = user["userInfo"]["locationInfo"]["z"];
                    //long bearing = user["userInfo"]["locationInfo"]["velocity"]["bearing"];
                    long bearing = user["userInfo"]["locationInfo"]["velocity"]["bearing"].is_null() ? 0 : user["userInfo"]["locationInfo"]["velocity"]["bearing"].get<long>();
                    long speed = user["userInfo"]["locationInfo"]["velocity"]["horizontalSpeed"];

                    UserLocation userLocation = UserLocation(x, y, z, bearing, speed);

                    // Upsert Logic
                    auto it = users.find(address);
                    if (it != users.end()) {
                        // Update existing user (preserves Radio Stats)
                        it->second.setAccessPointId(apData.getAccessPointId());
                        it->second.setCurrentLocation(userLocation);
                        // Recalculate distance to AP after location update
                        double newDistance = it->second.calculateDistanceToAP(
                            apData.getAccessPointLocation().getX(),
                            apData.getAccessPointLocation().getY(),
                            userLocation.getX(),
                            userLocation.getY()
                        );
                        it->second.setDistanceToAP(newDistance);
                    	omnetpp::simtime_t dataTime = simTime();
                    	it->second.setLsUpdate(dataTime);
                    	it->second.setLastUpdated(dataTime);

                    } else {
                        // Insert new user
                        UserData userData = UserData(address, apData, userLocation);
                    	omnetpp::simtime_t dataTime = simTime();
                    	userData.setLsUpdate(dataTime);
                    	userData.setLastUpdated(dataTime);
                        users[address] = userData;
                    }
                    hasPendingUpdates_ = true;  // Mark dirty for snapshot
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

    // print users
    for (auto& user : users)
    {
        EV << "LETS PRINT THE USERS" << endl;
        EV << "RavensAgentApp::handleLSMessage - User: " << user.first << " AccessPoint: " << user.second.getAccessPointId() << endl;
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
    EV << "RavensAgentApp::handleProcessedMessage - Message Received" <<  msg->getName() << endl;
    // check if the message is from the RavensController
    if(controllerSocket_.belongsToSocket(msg))
    {
        inet::Packet* packet = nullptr;
        try {
            packet = check_and_cast<inet::Packet*>(msg);
            auto received_packet = packet->peekAtFront<RavensLinkPacket>();
            EV << "RavensAgentApp::handleProcessedMessage: received message from Ravens Controller" << endl;
            if(received_packet->getType() == JOIN_NETWORK_ACK)
            {
                EV << "RavensAgentApp::handleProcessedMessage - JOIN_NETWORK_ACK received" << endl;
                cMessage *msg = new cMessage("connectMp1");
                scheduleAt(simTime() + 0, msg);
            }
            else if(received_packet->getType() == INFRAESTRUCTURE_DETAILS_ACK)
            {
                EV << "RavensAgentApp::handleProcessedMessage - INFRAESTRUCTURE_DETAILS received" << endl;
                auto infrastructureDetailsAck = packet->peekAtFront<RavensLinkInfrastructureDetailsMessageAck>();
                EV << "RavensAgentApp::handleProcessedMessage - Rate received: " << infrastructureDetailsAck->getRate() << endl;
                simtime_t interval = infrastructureDetailsAck->getRate();
                // convert to int
                int intervalInt = (int) interval.dbl();
                setRetrievalInterval(intervalInt/1000);
                cMessage *msg = new cMessage("sendUserListSub");
                scheduleAt(simTime() + 0, msg);
            }
            delete packet;
        } catch (const cRuntimeError& err)
        {
            std::cerr << "received uncastable msg with name " << msg->getName() << " of class " << msg->getClassName() << std::endl;
            delete msg;
            return;
        }
    }
    else{
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

void RavensAgentApp::socketClosed(inet::TcpSocket* socket)
{
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

simtime_t RavensAgentApp::getRetrievalInterval()
{
    return sendInterval;
}

void RavensAgentApp::setRetrievalInterval(simtime_t interval)
{
    sendInterval = interval;
}

std::string RavensAgentApp::getMecHostId()
{
    return mecHostId;
}

}
