#include "MigrateOnTime.h"

namespace simu5g {

void MigrateOnTime::reactOnUpdate(const std::vector<MigrationPrediction> &updatedList)
{
    // Flask communication has moved to the RavensController (SendToExternalServer policy).
    // This strategy is no longer used.
}

void MigrateOnTime::reactOnUpdate(const UserMEHUpdate &update)
{
    // not implemented
}

}
