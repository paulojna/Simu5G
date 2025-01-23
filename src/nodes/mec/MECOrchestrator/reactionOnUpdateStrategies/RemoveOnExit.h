#ifndef NODES_MEC_MECORCHESTRATOR_REMOVEONEXIT_H_
#define NODES_MEC_MECORCHESTRATOR_REMOVEONEXIT_H_

#include "ReactionOnUpdate.h"

#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

namespace simu5g {

class RemoveOnExit : public ReactionOnUpdate
{
  protected:
    virtual void reactOnUpdate(const simu5g::UserMEHUpdate&) override;
    virtual void reactOnUpdate(const std::vector<simu5g::UserEntryUpdate>&) override;
  public:
    RemoveOnExit(MecOrchestrator* mecOrchestrator):ReactionOnUpdate(mecOrchestrator){}
    virtual ~RemoveOnExit(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_REMOVEONEXIT_H_ */
