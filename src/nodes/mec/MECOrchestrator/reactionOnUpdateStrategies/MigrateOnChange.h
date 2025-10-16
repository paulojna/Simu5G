#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_

#include "ReactionOnUpdate.h"

//#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
//#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

class IOrchestratorApi;

namespace simu5g {

class MigrateOnChange : public ReactionOnUpdate
{
  public:
    using ReactionOnUpdate::ReactionOnUpdate; 
    virtual void reactOnUpdate(const simu5g::UserMEHUpdate&) override;
    virtual void reactOnUpdate(const std::vector<simu5g::UserEntryUpdate>&) override;
    virtual ~MigrateOnChange(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_ */