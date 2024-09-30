#ifndef __PACKETCOLLECTOR_H_
#define __PACKETCOLLECTOR_H_

#include <fstream>
#include <omnetpp.h>

namespace simu5g
{

using namespace omnetpp;

class PacketCollector : public cSimpleModule
{
    private:
        // the number of packets collected
        int numPackets;
        // the number of packets dropped
        int numPacketsDropped;
    
    public:
        PacketCollector();
        virtual ~PacketCollector();
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual void finish() override;

        void printPacketInfo(cMessage *msg);
};

}

#endif
