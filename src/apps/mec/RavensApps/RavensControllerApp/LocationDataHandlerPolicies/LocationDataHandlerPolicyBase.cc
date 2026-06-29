#include "LocationDataHandlerPolicyBase.h"
#include "../DataUpdates/UserMEHUpdate.h"

namespace simu5g {

void LocationDataHandlerPolicyBase::emitUserUpdate(const std::string& address,
                                                   const std::string& lastMeh,
                                                   const std::string& newMeh)
{
    UserMEHUpdate update;
    update.setAddress(address);
    update.setLastMEHId(lastMeh);
    update.setNewMEHId(newMeh);
    controllerApp_->userUpdates.insert_or_assign(address, update);
}

}
