#ifndef MIGRATIONPREDICTION_H
#define MIGRATIONPREDICTION_H

#include <string>
#include "omnetpp.h"

namespace simu5g {

// One expected future move of one user, as some model outside the simulation
// believes it. Unconfirmed by construction: a prediction that turns out wrong is
// corrected by a UserEvent, which is why the event stream stays on in every
// profile.
//
// The time is absolute. It used to be a delay measured from a separate
// timestamp field, which every consumer then had to add back together to get the
// only quantity any of them wanted — and MigrateOnPrediction adding it back was
// the sole reason the timestamp was carried at all.
struct MigrationPrediction
{
    std::string ueAddress;

    // Where the model believes the user is now, and where it is going. An empty
    // toMEHId is a predicted departure from the system rather than a move.
    std::string fromMEHId;
    std::string toMEHId;

    // The newest observation the model saw. Against expectedAt this gives the
    // horizon the model actually delivered; against the moment the orchestrator
    // acts, it gives how much of that horizon survived the pipeline.
    omnetpp::simtime_t observedAt = -1;

    // When the move is expected to happen — absolute simulation time, not a
    // delay.
    omnetpp::simtime_t expectedAt = -1;

    // How sure the model is, in whatever scale that model uses. Carried for
    // logging and comparison; nothing gates on it today.
    double confidence = -1.0;

    // A label for the model that produced this, for grouping runs during
    // analysis. Nothing may branch on it: which model spoke is the prediction
    // server's business, and keeping that true is what lets models be added
    // without touching this boundary.
    std::string modelId;

    bool isExitPrediction() const { return toMEHId.empty(); }
};

} // namespace simu5g

#endif // MIGRATIONPREDICTION_H
