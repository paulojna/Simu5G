#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_

#include "ReactionOnUpdate.h"

#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

namespace simu5g {

class MigrateOnChange : public ReactionOnUpdate
{
  protected:
    virtual void reactOnUpdate(const UserMEHUpdate&) override;
    virtual void reactOnUpdate(const std::vector<UserEntryUpdate>&) override;
  public:
    MigrateOnChange(MecOrchestrator* mecOrchestrator):ReactionOnUpdate(mecOrchestrator){}
    virtual ~MigrateOnChange(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_ */