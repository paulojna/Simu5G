#ifndef __UEPERFAPP_H_
#define __UEPERFAPP_H_

#include "inet/transportlayer/contract/udp/UdpSocket.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/networklayer/common/L3AddressResolver.h"

#include "apps/mec/EdgePerfApp/packets/RequestResponsePacket_m.h"

#include "common/binder/Binder.h"
#include "../RavensApps/UsersInfoPacket_m.h"

namespace simu5g {

#define UEAPP_REQUEST 0
#define MECAPP_RESPONSE 1
#define UEAPP_STOP 2
#define UEAPP_ACK_STOP 3

using namespace omnetpp;

struct requestMsg
{
    cMessage *requestMsg;
    unsigned int sno;
};

struct ackMsg
{
    unsigned int sno;
    inet::Packet* ack_packet;
};

class UEPerfApp: public cSimpleModule
{
    //communication to device app and mec app
    inet::UdpSocket socket;

    unsigned int sno_;
    int requestPacketSize_;
    double requestPeriod_;
    double requestTimeout_;
    unsigned int lostPackets_; 

    simtime_t start_;
    simtime_t end_;

    int mehostId_;

    // DeviceApp info
    int localPort_;
    int deviceAppPort_;
    inet::L3Address deviceAppAddress_;

    // Controller endPoint
    int controllerPort;
    inet::L3Address controllerAddress_;

    // MEC application endPoint (returned by the device app)
    inet::L3Address mecAppAddress_;
    int mecAppPort_;

    std::string mecAppName;

    // map to store the UE Requests while they are not confirmed
    std::map<unsigned int, inet::Ptr<RequestResponseAppPacket>> ueRequestMap;
    std::vector<UeTimeoutMessage*> ueTimeoutMsgs;
    std::vector<requestMsg*> ueRequestMsgs; 

    //scheduling
    cMessage *selfStart_;
    cMessage *selfStop_;
    cMessage *sendRequest_;
    cMessage *unBlockingMsg_; //it prevents to stop the send/response pattern if msg gets lost
    cMessage *printLostMessages_;

    //UeTimeoutMessage *sendRequestTimeout_;

    // signals for statistics
    simsignal_t processingTime_;
    simsignal_t serviceResponseTime_;
    simsignal_t upLinkTime_;
    simsignal_t downLinkTime_;
    simsignal_t responseTime_;
    simsignal_t lostMessages_;
    simsignal_t mecHostId_;

    simsignal_t ueAppId_;

    

  public:
    ~UEPerfApp();
    UEPerfApp();

  protected:

    virtual int numInitStages() const { return inet::NUM_INIT_STAGES; }
    void initialize(int stage);
    virtual void handleMessage(cMessage *msg);
    virtual void finish();

    void emitStats();

    // --- Functions to interact with the DeviceApp --- //
    void sendStartMECRequestApp();
    void sendStopMECRequestApp();
    void handleStopApp(cMessage* msg);
    void sendStopApp();

    void handleChangeMecHost(cMessage* msg);

    void handleAckStartMECRequestApp(cMessage* msg);
    void handleAckStopMECRequestApp(cMessage* msg);

    void handleUeTimeoutMessage(UeTimeoutMessage* msg);

    // --- Functions to interact with the MECPlatooningApp --- //
    void sendRequest();
    void recvResponse(cMessage* msg);

    void setMecAppAddress(inet::L3Address newAddress);
    void setMecAppPort(int newPort);

};

}

#endif
