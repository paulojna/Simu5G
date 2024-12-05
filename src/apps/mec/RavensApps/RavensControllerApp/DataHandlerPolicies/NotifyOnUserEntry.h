#ifndef RAVENS_CONTROLLER_APP_NOTIFYONUSERENTRY_H_
#define RAVENS_CONTROLLER_APP_NOTIFYONUSERENTRY_H_

#include "DataHandlerPolicyBase.h"
#include "../../RavensControllerUpdatePacket_m.h"
#include "../DataUpdates/UserEntryUpdate.h"
#include <string>
#include <unordered_map>

namespace simu5g
{

class NotifyOnUserEntry : public DataHandlerPolicyBase
{
  protected:
    std::unordered_map<std::string, UserEntryUpdate> standby;
    virtual inet::Packet *handleDataMessage(inet::Ptr<const RavensLinkUsersInfoSnapshotMessage> received_packet) override;

  public:
    NotifyOnUserEntry(RavensControllerApp *controllerApp);
    void addUserEntryUpdate(UserEntryUpdate &update);
    virtual ~NotifyOnUserEntry() {}

};

}

#endif