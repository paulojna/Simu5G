#include "RavensControllerApp.h"

#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "inet/transportlayer/contract/udp/UdpControlInfo_m.h"

#include "DataHandlerPolicies/SaveDataHistory.h"
#include "DataHandlerPolicies/NotifyOnDataChange.h"
#include "DataHandlerPolicies/NotifyOnUserEntry.h"

#define USERS_UPDATE 7
#define USERS_ENTRY 8
#define MAX_MEH_STATE_MAP_SIZE 15

namespace simu5g {

Define_Module(RavensControllerApp);

RavensControllerApp::RavensControllerApp(){
    dataHandlerPolicy_ = nullptr;
    calculateAvg_ = nullptr;
}

RavensControllerApp::~RavensControllerApp(){
    cancelAndDelete(calculateAvg_);
    udpSocket.close();
    hostsData.clear();
    hostsDataHistory.clear();
    delete dataHandlerPolicy_;
}

void RavensControllerApp::initialize(int stage){
    ApplicationBase::initialize(stage);
    if (stage!=inet::INITSTAGE_APPLICATION_LAYER)
        return;
    snapshot_frequency_ = par("snapshot_frequency");
    snapshot_starting_time_ = par("snapshot_starting_time");

    // start mehStateMap with a maximum size
    mehStateMap.reserve(MAX_MEH_STATE_MAP_SIZE);

    if(stage == inet::INITSTAGE_LOCAL){
        EV << "RavensControllerApp::initialize - stage " << stage << endl;
    }

    if(!strcmp(par("mode"), "SaveDataHistory")){
        EV << "RavensControllerApp::initialize - SaveDataHistory mode" << endl;
        dataHandlerPolicy_ = new SaveDataHistory(this, par("path"));
    }else if(!strcmp(par("mode"), "NotifyOnDataChange")){
        EV << "RavensControllerApp::initialize - NotifyOnDataChange handler mode" << endl;
        dataHandlerPolicy_ = new NotifyOnDataChange(this, par("threshold"));
    }else if(!strcmp(par("mode"), "NotifyOnUserEntry")){
        EV << "RavensControllerApp::initialize - NotifyOnUserEntry handler mode" << endl;
        dataHandlerPolicy_ = new NotifyOnUserEntry(this);
    }else{
        throw cRuntimeError("RavensControllerApp::initialize - invalid mode parameter");
    }

    if(gate("outGate")->isConnected()){
        EV << "RavensControllerApp::initialize - outGate is connected" << endl;
    }else{
        EV << "RavensControllerApp::initialize - outGate is not connected" << endl;
    }

    // TODO: add feature to calculate network metrics
    //calculateAvg_ = new cMessage("calculateAvgNetworkData");

    ravensLinkPacketFilter.setPattern("RavensLink*");
    uePacketFilter.setPattern("User*");

    scheduleAt(simTime() + snapshot_starting_time_, new cMessage("sendSnapshot"));
    // scheduleAt(simTime() + 10, calculateAvg_);
}

void RavensControllerApp::handleMessageWhenUp(cMessage *msg){
    EV << "RavensControllerApp::handleMessage - new message received" << endl;
    if(msg->isSelfMessage()){
        handleSelfMessage(msg);
    }else{
        udpSocket.processMessage(msg);
    }
}

void RavensControllerApp::handleStartOperation(inet::LifecycleOperation *operation){
    EV << "RavensControllerApp::handleStartOperation - start operation" << endl;
    int port = par("localPort");
    EV << "RavensControllerApp::initialize - binding to local port:" << port << endl;
    udpSocket.setOutputGate(gate("socketOut"));
    udpSocket.bind(port);
    udpSocket.setCallback(this);
}

void RavensControllerApp::handleStopOperation(inet::LifecycleOperation *operation){
    EV << "RavensControllerApp::handleStopOperation - start operation" << endl;
}

void RavensControllerApp::handleCrashOperation(inet::LifecycleOperation *operation){
    EV << "RavensControllerApp::handleCrashOperation - start operation" << endl;
}

void RavensControllerApp::handleSelfMessage(cMessage *msg){
    if(strcmp(msg->getName(), "sendSnapshot") == 0)
    {
        if(gate("outGate")->isConnected()){
            if(!userUpdates.empty())
            {
                inet::Packet *update = new inet::Packet("UserMEHUpdatedListMessage");
                auto userMEHUpdatedListMessage = inet::makeShared<UserMEHUpdatedListMessage>();
                userMEHUpdatedListMessage->setChunkLength(inet::B(1500));
                userMEHUpdatedListMessage->setType(USERS_UPDATE);
                userMEHUpdatedListMessage->setUeMehList(userUpdates);
                update->insertAtBack(userMEHUpdatedListMessage);
                send(update, "outGate");
                EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - report sent to MEO" << endl;

                //print userUpdates
                for(auto user : userUpdates){
                    EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - user address: " << user.getAddress() << " last MEH: " << user.getLastMEHId() << " new MEH: " << user.getNewMEHId() << endl;
                }

                userUpdates.clear();
            }
            else if(!userEntryUpdates.empty())
            {
                inet::Packet *entry = new inet::Packet("UserEntryListMessage");
                auto userEntryListMessage = inet::makeShared<UserEntryListMessage>();
                userEntryListMessage->setChunkLength(inet::B(1500));
                userEntryListMessage->setType(USERS_ENTRY);
                userEntryListMessage->setUeEntryList(userEntryUpdates);
                entry->insertAtBack(userEntryListMessage);
                send(entry, "outGate");
                EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - entry sent to MEO" << endl;
                userEntryUpdates.clear();
            }
            else{
                EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - nothing to send" << endl;
            }
        }else{
            EV << "RavensControllerApp::handleSelfMessagw::sendSnapshot - outGate is not connected" << endl;
        }
        scheduleAt(simTime() + snapshot_frequency_, msg);
        EV << "RavensControllerApp::handleSelfMessage::sendSnapshot - next snapshot scheduled" << endl;
    }
    else if(strcmp(msg->getName(), "calculateAvgNetworkData") == 0)
    {
        calculateAvgNetworkData();
    }
    else
    {
        EV << "RavensControllerApp::handleSelfMessage - unknown message" << endl;
    }
}

void RavensControllerApp::socketDataArrived(inet::UdpSocket *socket, inet::Packet *packet){
    EV << "RavensControllerApp::socketDataArrived - socket data arrived" << endl;
    inet::L3Address remoteAddress = packet->getTag<inet::L3AddressInd>()->getSrcAddress();
    int srcPort = packet->getTag<inet::L4PortInd>()->getSrcPort();

    if(ravensLinkPacketFilter.matches(packet))
    {
        EV << "RavensControllerApp::socketDataArrived - ravens link packet received" << endl;    
        // get the type of the message received
        auto received_packet = packet->peekAtFront<RavensLinkPacket>();
        if(received_packet->getType() == JOIN_NETWORK_REQUEST)
        {
            //cast the packet from RavensLinkPacket to the specific type of packet
            auto joinNetworkRequest = packet->peekAtFront<RavensLinkJoinNetworkRequestMessage>();

            EV << "RavensControllerApp::socketDataArrived - join network request received" << endl;

            // Create new MECHostState
            MECHostState newHostState;
            newHostState.mecHostId = joinNetworkRequest->getMecHostId();
            newHostState.lastUpdate = simTime();
    
            // Initialize host data
            newHostState.hostData.setHostId(joinNetworkRequest->getMecHostId());
            newHostState.hostData.setL3Address(remoteAddress);
            newHostState.hostData.setPort(srcPort);
    
            // Initialize metrics with default values
            newHostState.avgRTT = 0.0;
            newHostState.avgLostPackets = 0.0;

            mehStateMap[joinNetworkRequest->getMecHostId()] = newHostState;

            //send back a RAVENS_LINK_PACKET with type JOIN_NETWORK_ACK
            sendJoinNetworkAck(socket, remoteAddress, srcPort);
        }
        else if(received_packet->getType() == INFRAESTRUCTURE_DETAILS)
        {   
            EV << "RavensControllerApp::socketDataArrived - infrastructure details received" << endl;

            auto infrastructureDetails = packet->peekAtFront<RavensLinkInfrastructureDetailsMessage>();
            
            // Get and log the AP list from the message
            std::vector<AccessPointData> apList = infrastructureDetails->getAPList();
            EV << "RavensControllerApp::socketDataArrived - Received APs for host " << infrastructureDetails->getMecHostId() << ":" << endl;
            for(const auto& ap : apList) {
                EV << "AP ID: " << ap.getAccessPointId() << endl;
            }

            // Find the MEC host in our state map
            auto it = mehStateMap.find(infrastructureDetails->getMecHostId());
            if(it == mehStateMap.end()) {
                EV << "RavensControllerApp::socketDataArrived - host " << infrastructureDetails->getMecHostId() << " not found" << endl;
                return;
            }

            // Update the host data
            it->second.hostData.setAccessPoints(apList);
            it->second.lastUpdate = simTime();  // Update the state's last update time as well

            // Send acknowledgment
            sendInfrastructureDetailsAck(socket, remoteAddress, srcPort);

            EV << "RavensControllerApp::socketDataArrived - Updated infrastructure details for host " << it->first << ", now managing " << apList.size() << " access points" << endl;
        }
        else if(received_packet->getType() == USERS_INFO_SNAPSHOT)
        {
            auto usersInfoSnapshot = packet->peekAtFront<RavensLinkUsersInfoSnapshotMessage>();
            EV << "RavensControllerApp::socketDataArrived - users info snapshot received from MEC host: " << usersInfoSnapshot->getMecHostId() << " with a number of users of " << usersInfoSnapshot->getUsers().size() << endl;

            //call the data handler policy, depending on the mode we might want to do different things
            inet::Packet* response = dataHandlerPolicy_->handleDataMessage(packet->peekAtFront<RavensLinkUsersInfoSnapshotMessage>());
        }
    }
    else if(uePacketFilter.matches(packet))
    {
        EV << "RavensControllerApp::socketDataArrived - User Network Info Packet received" << endl;
        auto users_network_info = packet->peekAtFront<UsersNetworkInfoPacket>();
        // TODO: correct this when network metrics is implemented again
        // add the information to the hostsNetworkData map
        //hostsNetworkData[users_network_info->getMecHostId()].avgRTT.push_back(users_network_info->getAvgRTT());
        //hostsNetworkData[users_network_info->getMecHostId()].avgLostPackets.push_back(users_network_info->getLostPackets());
        delete packet;
    }
    else{
        EV << "RavensControllerApp::socketDataArrived - unknown packet received" << endl;
    }
}

void RavensControllerApp::calculateAvgNetworkData(){
    for(auto host : hostsNetworkData){
        double avg_RTT = 0;
        double avg_LostPackets = 0;
        for(auto rtt : host.second.avgRTT){
            avg_RTT += rtt;
        }
        for(auto lostPackets : host.second.avgLostPackets){
            avg_LostPackets += lostPackets;
        }

        host.second.lastAvgRTT = avg_RTT / host.second.avgRTT.size();
        host.second.lastAvgLostPackets = avg_LostPackets / host.second.avgLostPackets.size();
        host.second.lastUpdate = simTime();

        host.second.avgRTT.clear();
        host.second.avgLostPackets.clear();

        //print the calculated values
        //std::cout << " RavensControllerApp::calculateAvgNetworkData - host: " << host.first << " avgRTT: " << host.second.lastAvgRTT << " avgLostPackets: " << host.second.lastAvgLostPackets << endl;
        //std::cout << simTime() << " - RavensControllerApp::calculateAvgNetworkData - host: " << host.first << " avgRTT: " << host.second.lastAvgRTT << " avgLostPackets: " << host.second.lastAvgLostPackets << endl;
    }

    scheduleAt(simTime() + 5, calculateAvg_);
}

void RavensControllerApp::sendJoinNetworkAck(inet::UdpSocket *socket, inet::L3Address remoteAddress, int port){
    EV << "RavensControllerApp::sendJoinNetworkAck - sending join network ack" << endl;
    inet::Packet* packet = new inet::Packet("JoinNetworkAckMessage");
    auto request = inet::makeShared<RavensLinkPacket>();
    request->setChunkLength(inet::B(500));
    request->setType(JOIN_NETWORK_ACK);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    packet->insertAtBack(request);
    socket->sendTo(packet, remoteAddress, port);
}

void RavensControllerApp::sendInfrastructureDetailsAck(inet::UdpSocket *socket, inet::L3Address remoteAddress, int port){
    EV << "RavensControllerApp::sendInfrastructureDetailsAck - sending infrastructure details ack" << endl;
    inet::Packet* packet = new inet::Packet("RavensLinkInfrastructureDetailsAckMessage");
    auto request = inet::makeShared<RavensLinkInfrastructureDetailsMessageAck>();
    request->setChunkLength(inet::B(500));
    request->setType(INFRAESTRUCTURE_DETAILS_ACK);
    request->setRequestId(0);
    request->setTimeStamp(simTime().inUnit(SIMTIME_S));
    request->setInfoType(100);
    request->setRate(3000);
    packet->insertAtBack(request);
    socket->sendTo(packet, remoteAddress, port);
}

void RavensControllerApp::socketClosed(inet::UdpSocket *socket){
    EV << "RavensControllerApp::socketClosed - socket closed" << endl;
}   

void RavensControllerApp::socketErrorArrived(inet::UdpSocket *socket, inet::Indication *indication){
    EV << "RavensControllerApp::socketErrorArrived - socket error arrived" << endl;
}

// detect in userState if there are any user without an updade for the last treshold seconds, remove it also from the mehStateMap
std::vector<std::pair<std::string, std::string>> RavensControllerApp::detectInactiveUsers() {
    const simtime_t threshold = par("threshold");
    const simtime_t currentTime = simTime();
    int removedCount = 0;
    
    // Vector to store removed users info: <userId, mehId>
    std::vector<std::pair<std::string, std::string>> removedUsers;
    
    auto userIt = userStateMap.begin();
    while (userIt != userStateMap.end()) {
        const std::string& userId = userIt->first;
        const UserState& userState = userIt->second;
        
        if (currentTime - userState.lastUpdate > threshold) {
            EV << "User " << userId << " inactive for " << (currentTime - userState.lastUpdate) << " seconds (threshold: " << threshold << ")" << endl;

            if (!userState.currentMEH.empty()) {
                auto mehIt = mehStateMap.find(userState.currentMEH);
                if (mehIt != mehStateMap.end()) {
                    try {
                        auto userDataIt = mehIt->second.hostData.getUsers().find(userId);
                        if (userDataIt != mehIt->second.hostData.getUsers().end()) {
                            mehIt->second.hostData.getUsers().erase(userId);
                            
                            // Update MEH timestamps
                            mehIt->second.lastUpdate = currentTime;
                            
                            EV << "Updated MEH " << mehIt->first << ", remaining users: " << mehIt->second.hostData.getUsers().size() << endl;
                        }
                        // Store the removed user info
                        removedUsers.push_back({userId, userState.currentMEH});
                    } catch (const std::exception& e) {
                        EV << "Error updating MEH " << mehIt->first << ": " << e.what() << endl;
                    }
                } else {
                    // this should never happen
                    EV << "Warning: User's MEH " << userState.currentMEH << " not found in mehStateMap" << endl;
                }
            } else {
                // this should never happen also
                EV << "Warning: User " << userId << " has no current MEH" << endl;
            }

            userIt = userStateMap.erase(userIt);
            removedCount++;
        } else {
            ++userIt;
        }
    }

    if (removedCount > 0) {
        EV << "Cleanup complete: removed " << removedCount << " inactive users. Remaining users: " << userStateMap.size() << endl;
    }
    
    return removedUsers;
}


} // namespace


