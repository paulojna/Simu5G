#include "MigrateOnChange.h"
  #include "nodes/mec/MECOrchestrator/services/MecAppMigrationManager/MecAppMigrationManager.h"
#include "nodes/mec/MECOrchestrator/MECOMessages/MECOrchestratorMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_m.h"
#include "nodes/mec/UALCMP/UALCMPMessages/UALCMPMessages_types.h"

/*
* MigrateOnChange Strategy
*
* This strategy reacts to RAVENS Controller updates about UE movement.
* It handles three different scenarios:
*
* 1) newMEHId is empty (" ") → UE left the system → Remove app
*
* 2) newMEHId and lastMEHId both non-empty AND different → UE migrated between MEHs → Migrate app
*
* 3) lastMEHId is empty (" ") but newMEHId is not → RAVENS detecting UE for first time → Check if migration needed
*    (UE might not have app yet, might be on correct MEH, or need migration)
*/

namespace simu5g {

using namespace omnetpp;

void MigrateOnChange::reactOnUpdate(const std::vector<UserEntryUpdate> &updatedList)
{
    // not implemented
}

void MigrateOnChange::reactOnUpdate(const UserMEHUpdate &update)
{
    // scenario 1 - (described above)
    if (update.getNewMEHId()==" ")
    {
        EV << "MigrateOnChange::reactOnUpdate - UE left system!" << endl;
        EV << "  UE: " << update.getAddress() << endl;
        EV << "  Last MEH: " << update.getLastMEHId() << endl;

        api_->removeAppFromSystem(update.getAddress(), update.getLastMEHId());
    }
    // scenario 2
    else if(update.getNewMEHId()!=" " && update.getLastMEHId()!=" " && update.getNewMEHId()!=update.getLastMEHId())
    {
        EV << "MigrateOnChange::reactOnUpdate - Migration between MEHs detected" << endl;
        EV << "  UE: " << update.getAddress() << endl;
        EV << "  From: " << update.getLastMEHId() << " To: " << update.getNewMEHId() << endl;

        MigrationResult result = api_->migrateApp(update.getAddress(), update.getNewMEHId(), update.getLastMEHId());

        if (!result.success) 
        {
            EV << "MigrateOnChange::reactOnUpdate - Migration failed: " << result.errorMessage << endl;
            // Migration failed - UE continues using old MEH endpoint
            // Fallback strategy here?
        } 
        else 
        {
            EV << "MigrateOnChange::reactOnUpdate - Migration initiated successfully" << endl;
            EV << "  Request Number: " << result.requestNumber << endl;
            EV << "  New Context ID: " << result.contextId << endl;
        }
    }
    // scenario 3
    else if (update.getLastMEHId()==" " && update.getNewMEHId()!=" ")
    {
        EV << "MigrateOnChange::reactOnUpdate - New UE detected by RAVENS" << endl;
        EV << "  UE: " << update.getAddress() << endl;
        EV << "  Target MEH: " << update.getNewMEHId() << endl;

        // This method handles three sub-cases:
        // - UE has no app yet → Returns "No migration needed"
        // - UE already on target MEH → Returns "No migration needed"
        // - UE on different MEH → Triggers migration
        MigrationResult result = api_->checkIfMigrationIsNeeded(update.getAddress(), update.getNewMEHId(), update.getLastMEHId());

        if (result.success) {
            EV << "MigrateOnChange::reactOnUpdate - Migration was needed and initiated" << endl;
            EV << "  Request Number: " << result.requestNumber << endl;
        } else {
            // Normal cases: "No migration needed"
            // Could be: no app instantiated yet, or already on correct MEH
            EV << "MigrateOnChange::reactOnUpdate - " << result.errorMessage << endl;
        }
    }
    else
    {
        EV << "MigrateOnChange::reactOnUpdate - Unexpected RAVENS update" << endl;
        EV << "  UE: " << update.getAddress() << endl;
        EV << "  NewMEH: '" << update.getNewMEHId() << "'" << endl;
        EV << "  LastMEH: '" << update.getLastMEHId() << "'" << endl;
        EV << "  No action taken" << endl;
    }
}

}
