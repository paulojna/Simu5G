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

    delete dataHandlerPolicy_;
}

void RavensControllerApp::initialize(int stage){
    ApplicationBase::initialize(stage);
    if (stage!=inet::INITSTAGE_APPLICATION_LAYER)
        return;
    snapshot_frequency_ = par("snapshot_frequency");
    snapshot_starting_time_ = par("snapshot_starting_time");
    threshold_ = par("threshold");
    update = nullptr;

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
            MECHostData newHostData;
            newHostData.setHostId(joinNetworkRequest->getMecHostId());
            newHostData.setL3Address(remoteAddress);
            newHostData.setPort(srcPort);

            mehStateMap[joinNetworkRequest->getMecHostId()] = newHostData;

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
            it->second.setAccessPoints(apList);
            // Send acknowledgment
            sendInfrastructureDetailsAck(socket, remoteAddress, srcPort);

            EV << "RavensControllerApp::socketDataArrived - Updated infrastructure details for host " << it->first << ", now managing " << apList.size() << " access points" << endl;
        }
        else if(received_packet->getType() == USERS_INFO_SNAPSHOT)
        {
            update = dataHandlerPolicy_->handleDataMessage(packet->peekAtFront<RavensLinkUsersInfoSnapshotMessage>());
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
    // TODO: to develop when network metrics is implemented again
    /*
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
    */
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

/*
    Method that will run through the userStateMap and detect users that have not been updated for a pre-determined 
    amount time - defined by the treshold_.
*/
std::vector<UserState> RavensControllerApp::removeInactiveUsers(){
    simtime_t actual = simTime();
    std::vector<UserState> inactiveUsers;

    auto it = userStateMap.begin();
    while (it != userStateMap.end()) {
        if(actual - it->second.timestamp > threshold_){
            // add the user to the list of inactive users
            inactiveUsers.push_back(it->second);
            it = userStateMap.erase(it);  // erase() returns iterator to next element
        } else {
            ++it;
        }
    }

    return inactiveUsers;
}

/*
    Method to update the state of the userStateMap. It receives a RavensLinkUsersInfoSnapshotMessage message,
    checks if each user is already in the map and updates the data if it is.
*/
void RavensControllerApp::updateUserStateMap(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet){
    auto usersInfoSnapshot = received_packet;

    // check if the user is already in the map, if so, update the data, if not, add it
    for(const auto& user : usersInfoSnapshot->getUsers()){
        auto userIt = userStateMap.find(user.first);
        if(userIt == userStateMap.end()){
            // user is not in the map, we need to add it
            userStateMap[user.first].userId = user.second.getAddress();
            userStateMap[user.first].currentMEH = usersInfoSnapshot->getMecHostId();
            userStateMap[user.first].timestamp = usersInfoSnapshot->getTimeStamp();
            userStateMap[user.first].userData = user.second;
        }else{
            // user is in the map, we need to update the data in userStateMap
            userStateMap[user.first].currentMEH = usersInfoSnapshot->getMecHostId();  
            userStateMap[user.first].timestamp = usersInfoSnapshot->getTimeStamp();
            userStateMap[user.first].userData = user.second; 
        }
    }
}


/*
    Method to update the state of the userStateMap. It receives a RavensLinkUsersInfoSnapshotMessage message from a given MEH. 
    It should check if each user is already in the map and update the data if it is.
    If a given user is not in the map, it should be added. 
    It should also understand if the user changed MEH or position and create a list of changes to be returned to whoever called the update.
std::vector<UserStateChange> RavensControllerApp::updateUserStateMap(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet){
    std::vector<UserStateChange> changes;

    auto usersInfoSnapshot = received_packet;

    // first we should check if there are users in the userStateMap which have a timestamp older than the actual time - the threshold parameter
    auto it = userStateMap.begin();
    while (it != userStateMap.end()) {
        if(simTime() - it->second.timestamp > par("threshold")){
            changes.push_back({CHANGE_EXIT, it->second.userData});
            //std::cout << simTime() << " - RavensControllerApp::updateUserStateMap - we detected that user " << it->first << " has exited the network" << endl;
            it = userStateMap.erase(it);  // erase() returns iterator to next element
        } else {
            ++it;
        }
    }

    for(const auto& user : usersInfoSnapshot->getUsers()){
        auto userIt = userStateMap.find(user.first);
        if(userIt == userStateMap.end()){
            // user is not in the map, we need to add it
            userStateMap[user.first].userId = user.second.getAddress();
            userStateMap[user.first].currentMEH = usersInfoSnapshot->getMecHostId();
            userStateMap[user.first].timestamp = usersInfoSnapshot->getTimeStamp();
            userStateMap[user.first].userData = user.second;
            changes.push_back({CHANGE_EXIT, it->second.userData});
            //std::cout << simTime() << " - RavensControllerApp::updateUserStateMap - we detected that user " << user.first << " has entered the network through " << usersInfoSnapshot->getMecHostId() << endl;
        }else{
            // if the user is acr:10.0.15.66 print the timestamp received from the packet
            

            // first we should check if the timestamp of the new user data is greater than the timestamp of the user in the map
            if(usersInfoSnapshot->getTimeStamp() > userStateMap[user.first].timestamp){
                // user is in the map, we need to update the data in userStateMap, but first we need to check if the user has changed MEH or position
                if(userStateMap[user.first].currentMEH != usersInfoSnapshot->getMecHostId()){
                    changes.push_back({CHANGE_EXIT, it->second.userData});
                    //std::cout << simTime() << " - RavensControllerApp::updateUserStateMap - we detected that user " << user.first << " has changed to " << usersInfoSnapshot->getMecHostId() << endl;
                }else if (userStateMap[user.first].userData.getCurrentLocation() == user.second.getCurrentLocation()){
                    changes.push_back({CHANGE_EXIT, it->second.userData});    
                    // std::cout << simTime() << " - RavensControllerApp::updateUserStateMap - we detected that user " << user.first << " has not changed position" << endl;
                }else{
                    changes.push_back({CHANGE_EXIT, it->second.userData});
                    //std::cout << simTime() << " - RavensControllerApp::updateUserStateMap - we detected that user " << user.first << " has changed position" << endl;
                }
            
                userStateMap[user.first].currentMEH = usersInfoSnapshot->getMecHostId();  
                userStateMap[user.first].timestamp = usersInfoSnapshot->getTimeStamp();
                userStateMap[user.first].userData = user.second; 
            }
            else{
                // discard the new user data because it is older than the data in the map
                std::cout << simTime() << " -  RavensControllerApp::updateUserStateMap - discarding new user data because it is older than the data in the map" << endl;
            }
        }
    }

    return changes;
}
    */


} // namespace


