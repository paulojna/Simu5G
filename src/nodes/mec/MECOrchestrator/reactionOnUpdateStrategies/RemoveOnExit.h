#ifndef NODES_MEC_MECORCHESTRATOR_REMOVEONEXIT_H_
#define NODES_MEC_MECORCHESTRATOR_REMOVEONEXIT_H_

#include "ReactionOnUpdate.h"

class IOrchestratorApi;

namespace simu5g {

// Tears down a user's application when the user leaves the system, and does
// nothing else. Consumes only the event stream.
class RemoveOnExit : public ReactionOnUpdate
{
  public:
    using ReactionOnUpdate::ReactionOnUpdate;
    virtual void reactOnUpdate(const simu5g::UserEvent&) override;
    virtual ~RemoveOnExit(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_REMOVEONEXIT_H_ */
