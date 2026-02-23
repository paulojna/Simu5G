#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_

#include "ReactionOnUpdate.h"

namespace simu5g {

// NOTE: Flask communication has moved to RavensController (SendToExternalServer).
// This class is kept as a stub for backwards compatibility.
class MigrateOnTime : public ReactionOnUpdate
{
  public:
    using ReactionOnUpdate::ReactionOnUpdate; 
    virtual void reactOnUpdate(const simu5g::UserMEHUpdate&) override;
    virtual void reactOnUpdate(const std::vector<simu5g::MigrationPrediction>&) override;
    virtual ~MigrateOnTime(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONTIME_H_ */