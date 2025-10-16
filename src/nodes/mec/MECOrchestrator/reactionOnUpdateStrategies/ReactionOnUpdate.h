#ifndef NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_
#define NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_


#include "nodes/mec/MECOrchestrator/Interfaces/IOrchestrationApi.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/UserMEHUpdate.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/UserEntryUpdate.h"

namespace simu5g {

class MecOrchestrator;

class ReactionOnUpdate
{
  protected:
    IOrchestratorApi* api_ = nullptr;

  public:
    explicit ReactionOnUpdate(IOrchestratorApi* api) : api_(api) {}
    virtual void reactOnUpdate(const UserMEHUpdate&) = 0;
    virtual void reactOnUpdate(const std::vector<UserEntryUpdate>&) = 0;
    virtual ~ReactionOnUpdate() {}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_ */