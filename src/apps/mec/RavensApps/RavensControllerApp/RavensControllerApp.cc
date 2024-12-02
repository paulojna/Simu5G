#include "RavensControllerApp.h"

#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/transportlayer/common/L4PortTag_m.h"
#include "inet/transportlayer/contract/udp/UdpControlInfo_m.h"

#include "DataHandlerPolicies/SaveDataHistory.h"
#include "DataHandlerPolicies/NotifyOnDataChange.h"

#define USERS_UPDATE 7
#define USERS_ENTRY 8

namespace simu5g {

Define_Module(RavensControllerApp);

RavensControllerApp::RavensControllerApp(){
    dataHandlerPolicy_ = nullptr;
    calculateAvg_ = nullptr;
    userUpdates.clear();
}

RavensControllerApp::~RavensControllerApp(){
    cancelAndDelete(calculateAvg_);
    udpSocket.close();
    hostsData.clear();
    hostsDataHistory.clear();
}

void RavensControllerApp::initialize(int stage){
    ApplicationBase::initialize(stage);
    //bufferTime = par("bufferTime");
    if (stage!=inet::INITSTAGE_APPLICATION_LAYER)
        return;
    snapshot_frequency_ = par("snapshot_frequency");
    snapshot_starting_time_ = par("snapshot_starting_time");

    std::cout << "Stage" << stage << endl;

    if(stage == inet::INITSTAGE_LOCAL){
        EV << "RavensControllerApp::initialize - stage " << stage << endl;
    }
    EV << "RavensControllerApp::initialize - stage " << stage << endl;

    if(!strcmp(par("mode"), "SaveDataHistory")){
        EV << "RavensControllerApp::initialize - SaveDataHistory mode" << endl;
        dataHandlerPolicy_ = new SaveDataHistory(this, par("path"));
    }else if(!strcmp(par("mode"), "NotifyOnDataChange")){
        EV << "RavensControllerApp::initialize - NotifyOnDataChange handler mode" << endl;
        dataHandlerPolicy_ = new NotifyOnDataChange(this, par("treshold"));
    }else{
        throw cRuntimeError("RavensControllerApp::initialize - invalid mode parameter");
    }

    if(gate("outGate")->isConnected()){
        EV << "RavensControllerApp::initialize - outGate is connected" << endl;
    }else{
        EV << "RavensControllerApp::initialize - outGate is not connected" << endl;
    }

    calculateAvg_ = new cMessage("calculateAvgNetworkData");

    ravensLinkPacketFilter.setPattern("RavensLink*");
    uePacketFilter.setPattern("User*");

    scheduleAt(simTime() + snapshot_starting_time_, new cMessage("sendSnapshot"));
    scheduleAt(simTime() + 10, calculateAvg_);
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
            if(userUpdates.size() > 0)
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
            //fill the hostsData map with the new host. Key and MecHostid in the MECHostData object are the same
            MECHostData hostData;
            mecHostNetworkData mehNetworkData;
            hostData.setHostId(joinNetworkRequest->getMecHostId());
            mehNetworkData.mecHostId = joinNetworkRequest->getMecHostId();
            hostData.setLastUpdated(simTime());
            hostData.setL3Address(remoteAddress);
            hostData.setPort(srcPort);
            hostsData.insert({joinNetworkRequest->getMecHostId(),hostData});
            hostsNetworkData[joinNetworkRequest->getMecHostId()] = mehNetworkData;
            
            //send back a RAVENS_LINK_PACKET with type JOIN_NETWORK_ACK
            sendJoinNetworkAck(socket, remoteAddress, srcPort);

            //print hostsData map state
            for(auto host : hostsData){
                EV << "RavensControllerApp::socketDataArrived printing hostsData content - host: " << host.first << endl;
            }
        }
        else if(received_packet->getType() == INFRAESTRUCTURE_DETAILS)
        {   
            EV << "RavensControllerApp::socketDataArrived - infrastructure details received" << endl;

            auto infrastructureDetails = packet->peekAtFront<RavensLinkInfrastructureDetailsMessage>();
            
            //get the APList from the message and print it to the console
            std::vector<AccessPointData> apList = infrastructureDetails->getAPList();
            for(auto ap : apList){
                EV << "RavensControllerApp::socketDataArrived - AP: " << ap.getAccessPointId() <<  endl;
            }

            std::map<std::string, MECHostData>::iterator it = hostsData.find(infrastructureDetails->getMecHostId());
            if(it == hostsData.end()){
                EV << "RavensControllerApp::socketDataArrived - host not found" << endl;
                return;
            }
            it->second.setAccessPoints(apList);
            it->second.setLastUpdated(simTime());

            //send back a INFRAESTRUCTURE_DETAILS_ACK
            sendInfrastructureDetailsAck(socket, remoteAddress, srcPort);

            //print the hostsData map state with regards to the access points
            for(auto host : hostsData){
                EV << "RavensControllerApp::socketDataArrived printing hostsData content - host: " << host.first << endl;
                for(auto ap : host.second.getAccessPoints()){
                    if(ap.getAccessPointId() != ""){
                        EV << "RavensControllerApp::socketDataArrived printing hostsData content - AP: " << ap.getAccessPointId() << endl;
                    }
                }
            }
        }
        else if(received_packet->getType() == USERS_INFO_SNAPSHOT)
        {
            auto usersInfoSnapshot = packet->peekAtFront<RavensLinkUsersInfoSnapshotMessage>();
            EV << "RavensControllerApp::socketDataArrived - users info snapshot received from MEC host: " << usersInfoSnapshot->getMecHostId() << " with a number of users of " << usersInfoSnapshot->getUsers().size() << endl;

            //call the data handler policy
            inet::Packet* response = dataHandlerPolicy_->handleDataMessage(packet->peekAtFront<RavensLinkUsersInfoSnapshotMessage>());
        }
    }
    else if(uePacketFilter.matches(packet))
    {
        EV << "RavensControllerApp::socketDataArrived - User Network Info Packet received" << endl;
        auto users_network_info = packet->peekAtFront<UsersNetworkInfoPacket>();
        // add the information to the hostsNetworkData map
        hostsNetworkData[users_network_info->getMecHostId()].avgRTT.push_back(users_network_info->getAvgRTT());
        hostsNetworkData[users_network_info->getMecHostId()].avgLostPackets.push_back(users_network_info->getLostPackets());
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

}



