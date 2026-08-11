#ifndef NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_
#define NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_

#include "ReactionOnUpdate.h"

class IOrchestratorApi;

namespace simu5g {

// The reactive mode: acts only on changes that have already happened. Consumes
// the event stream and nothing else, which makes it the baseline the proactive
// and learning modes are measured against.
class MigrateOnChange : public ReactionOnUpdate
{
  public:
    using ReactionOnUpdate::ReactionOnUpdate;
    virtual void reactOnUpdate(const simu5g::UserEvent&) override;
    virtual ~MigrateOnChange(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MIGRATEONCHANGE_H_ */
