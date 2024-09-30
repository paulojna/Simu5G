#include "nodes/mec/VirtualisationInfrastructure/PacketCollector.h"

namespace simu5g
{

Define_Module(PacketCollector);

PacketCollector::PacketCollector()
{
    numPackets = 0;
    numPacketsDropped = 0;
}

PacketCollector::~PacketCollector()
{
}

void PacketCollector::initialize()
{
    numPackets = 0;
    numPacketsDropped = 0;
}

void PacketCollector::handleMessage(cMessage *msg)
{
    printPacketInfo(msg);  
    numPackets++;
    delete msg;
}

void PacketCollector::finish()
{
    EV << "PacketCollector::finish" << endl;
}

void PacketCollector::printPacketInfo(cMessage *msg)
{
    std::cout << "Packet received at the PacketCollector module: " << msg->getName() << " with content " << msg << std::endl;
}

}
