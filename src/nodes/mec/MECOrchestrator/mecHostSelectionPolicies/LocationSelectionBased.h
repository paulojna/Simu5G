#ifndef NODES_MEC_MECORCHESTRATOR_MECHOSTSELECTIONPOLICIES_LOCATIONSELECTIONBASED_H_
#define NODES_MEC_MECORCHESTRATOR_MECHOSTSELECTIONPOLICIES_LOCATIONSELECTIONBASED_H_

#include "nodes/mec/MECOrchestrator/mecHostSelectionPolicies/SelectionPolicyBase.h"

namespace simu5g {

using namespace omnetpp;

class LocationSelectionBased : public SelectionPolicyBase
{
    protected:
        virtual cModule* findBestMecHost(const ApplicationDescriptor&, inet::L3Address) override;
    public:
        LocationSelectionBased(MecOrchestrator* mecOrchestrator):SelectionPolicyBase(mecOrchestrator){}
      virtual ~LocationSelectionBased(){}
};

}

#endif /* NODES_MEC_MECORCHESTRATOR_MECHOSTSELECTIONPOLICIES_LOCATIONSELECTIONBASED_H_ */
