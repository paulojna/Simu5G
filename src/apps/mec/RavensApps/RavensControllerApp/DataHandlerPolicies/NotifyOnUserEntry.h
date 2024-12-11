#ifndef RAVENS_CONTROLLER_APP_NOTIFYONUSERENTRY_H_
#define RAVENS_CONTROLLER_APP_NOTIFYONUSERENTRY_H_

#include "DataHandlerPolicyBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include "../DataUpdates/UserEntryUpdate.h"
#include <string>
#include <unordered_map>

namespace simu5g
{

struct UeSpeedInfo
{
    std::string ueId;
    long speed;
    std::string bsId;
    omnetpp::simtime_t timestamp;

    UeSpeedInfo(const std::string& ueId, long speed, const std::string& bsId, omnetpp::simtime_t timestamp)
        : ueId(ueId), speed(speed), bsId(bsId), timestamp(timestamp) {}
};

class NotifyOnUserEntry : public DataHandlerPolicyBase
{
  protected:
    std::unordered_map<std::string, UserEntryUpdate> standby;
    std::unordered_map<std::string, UeSpeedInfo> speedInfoList;
    virtual inet::Packet *handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet) override;

  public:
    NotifyOnUserEntry(RavensControllerApp *controllerApp);
    void addUserEntryUpdate(UserEntryUpdate &update);
    int calculateNumberOfUsers(std::string baseStationId);
    double calculateAvgSpeed(std::string baseStationId);
    int calculateNumberOfUsersLessSpeed(std::string baseStationId, long speed);
    virtual ~NotifyOnUserEntry();

};

}

#endif
