#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_

#include "ReactionOnUpdate.h"

#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

namespace simu5g {

class MigrateOnTime : public ReactionOnUpdate
{
  private:
    std::string url = "http://localhost:5002/duration";
  public:
    using ReactionOnUpdate::ReactionOnUpdate; 
    virtual void reactOnUpdate(const simu5g::UserMEHUpdate&) override;
    virtual void reactOnUpdate(const std::vector<simu5g::UserEntryUpdate>&) override;
    virtual ~MigrateOnTime(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_ */