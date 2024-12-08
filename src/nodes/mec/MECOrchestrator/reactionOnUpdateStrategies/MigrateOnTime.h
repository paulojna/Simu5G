#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_

#include "ReactionOnUpdate.h"

#include "nodes/mec/MECPlatformManager/MecPlatformManager.h"
#include "nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.h"

namespace simu5g {

class MigrateOnTime : public ReactionOnUpdate
{
  protected:
  virtual void reactOnUpdate(const UserMEHUpdate&) override;
    virtual void reactOnUpdate(const std::vector<UserEntryUpdate>&) override;
  public:
    MigrateOnTime(MecOrchestrator* mecOrchestrator):ReactionOnUpdate(mecOrchestrator){}
    virtual ~MigrateOnTime(){}
  private:
    std::string url = "http://localhost:5001/duration";
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_ */