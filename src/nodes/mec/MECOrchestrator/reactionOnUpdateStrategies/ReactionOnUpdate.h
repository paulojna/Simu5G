#ifndef NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_
#define NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_


#include "nodes/mec/MECOrchestrator/MecOrchestrator.h"
#include "apps/mec/RavensApps/RavensControllerApp/DataUpdates/UserMEHUpdate.h"

namespace simu5g {

class MecOrchestrator;

class ReactionOnUpdate
{
    friend class MecOrchestrator;

  protected:
    MecOrchestrator* mecOrchestrator_;
    virtual void reactOnUpdate(const UserMEHUpdate&) = 0;

  public:
    ReactionOnUpdate(MecOrchestrator* mecOrchestrator){mecOrchestrator_ = mecOrchestrator;}
    virtual ~ReactionOnUpdate() {}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_REACTIONONUPDATE_H_ */