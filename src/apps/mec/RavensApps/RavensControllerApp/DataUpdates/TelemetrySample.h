#ifndef RAVENS_CONTROLLER_APP_TELEMETRYSAMPLE_H_
#define RAVENS_CONTROLLER_APP_TELEMETRYSAMPLE_H_

// The canonical per-UE and per-cell observation records, and the only functions
// that build them.
//
// These used to live in the telemetry sink base class, back when the sinks were
// the only consumers. They are here now because the orchestrator receives raw
// telemetry too, so RavensControlPacket.msg has to name these types — and it
// cannot include a sink header without dragging in the whole Controller.
//
// Nothing about the records changed in the move. The field lists are what the
// models are trained on, so they are the last thing that should shift while
// something else is being rearranged.

#include "apps/mec/RavensApps/RavensLinkPacket_m.h"
#include "apps/mec/RavensApps/RavensAgentApp/UserData.h"
#include "apps/mec/RavensApps/RavensAgentApp/AccessPointRadioInfoData.h"

#include <string>
#include <vector>

namespace simu5g {

using namespace omnetpp;

// One observation of one UE, in the single form every consumer sees.
//
// WHY THIS EXISTS
// The CSV columns and the prediction payload used to be assembled by two
// separate pieces of code that happened to agree. Nothing enforced that, and
// they had already drifted: the CSV wrote 20 fields and the payload 8, so nine
// quantities — both delays, both PDRs, both data volumes, RSRP, the RNIS cell
// id and the radio timestamp — could be trained on and would never be served.
// Both consumers now walk the same field list, so a field added here reaches
// both by construction rather than by remembering.
//
// The names in forEachField() are the CSV column names, and the prediction
// payload uses them verbatim as its JSON keys. One name per quantity, end to
// end: a renamed column cannot quietly become a differently named feature.
struct UserSample
{
    // When the frame carrying this observation left the Agent. Identical for
    // every sample in a frame, so it says nothing about when the observation
    // was taken — that is locationTimestamp, which is the key to use.
    simtime_t frameSentAt;

    // When each half of the record was measured. They are separate because the
    // Location Service and the RNIS run on independent cycles: a row is not a
    // snapshot of one instant, and a radio value repeated across two samples is
    // recognisable as a repeat by its unchanged timestamp.
    simtime_t locationTimestamp;
    simtime_t radioTimestamp;

    std::string userId;

    // The host that observed this sample. Exact, not inferred: a host's
    // Location Service only lists UEs attached to that host's own cells, so the
    // sample appearing in this host's frame *is* the observation.
    std::string observedMEH;

    // Where the Controller believed the UE was when this sample was processed.
    // It lags observedMEH by the reporting delay plus the exit confirmation
    // window, so the two disagree for a second or two around every handover —
    // that gap is a measurement of control-plane lag, not an error.
    //
    // The Controller keeps no history of its own past beliefs, so this is the
    // belief at the only moment it held one about this sample — when the frame
    // carrying it arrived. It therefore resolves to a frame, not to a second.
    //
    // Empty means the Controller had no record of the UE at all when the frame
    // arrived, which happens in three ways:
    //   - not confirmed yet: telemetry buffers a UE from its first Location
    //     Service tick, while an ENTRY takes several consecutive ticks to
    //     confirm, so an arrival close to a frame boundary ships unannounced;
    //   - never confirmed: a UE that brushed past for a tick or two and left,
    //     which the event channel deliberately never reports, but whose
    //     observations are still carried here;
    //   - already retired: a UE whose exit was confirmed and whose exit window
    //     elapsed, leaving buffered samples to ship in a later frame.
    //
    // Diagnostic only: trajectories are built from observedMEH.
    std::string confirmedMEH;

    std::string accessPointId;   // serving cell, as the Location Service names it
    std::string rnisCellId;      // serving cell, as the RNIS names it

    long x = 0;
    long y = 0;
    long z = 0;
    long speed = 0;
    long bearing = 0;
    double distanceToAccessPoint = -1.0;

    // Per-UE radio metrics. -1 means "not measured": the RNIS omits a field
    // whose collector has no data, and it is written as the sentinel rather
    // than left out, so every sample has the same shape.
    double dlNongbrDelayUe = -1.0;
    double ulNongbrDelayUe = -1.0;
    double dlNongbrPdrUe = -1.0;
    double ulNongbrPdrUe = -1.0;
    double dlNongbrDataVolumeUe = -1.0;
    double ulNongbrDataVolumeUe = -1.0;
    double rsrp = -1.0;

    // The one field list. Every consumer walks it: the CSV header, the CSV rows
    // and the prediction payload. Nothing else may enumerate these fields.
    //
    // It takes the function to call, and calls it once per field with that
    // field's name and value. The CSV writer passes a function that writes the
    // value into the file; the payload builder passes one that sets a JSON key.
    // Being a template, each caller's function is substituted in and inlined —
    // the CSV path compiles down to the same sequence of stream writes it would
    // have had if the columns were listed out by hand. Nothing is collected or
    // stored: this is unrelated to cComponent::emit() and result recording.
    template <typename WriteFn>
    void forEachField(WriteFn write) const
    {
        write("TimestampSent",         frameSentAt);
        write("LocationTimestamp",     locationTimestamp);
        write("RadioTimestamp",        radioTimestamp);
        write("UEId",                  userId);
        write("MEHId",                 observedMEH);
        write("ConfirmedMEH",          confirmedMEH);
        write("AccessPointId",         accessPointId);
        write("RNISCellId",            rnisCellId);
        write("x",                     x);
        write("y",                     y);
        write("z",                     z);
        write("Speed",                 speed);
        write("Bearing",               bearing);
        write("DistanceToAccessPoint", distanceToAccessPoint);
        write("DlNongbrDelayUe",       dlNongbrDelayUe);
        write("UlNongbrDelayUe",       ulNongbrDelayUe);
        write("DlNongbrPdrUe",         dlNongbrPdrUe);
        write("UlNongbrPdrUe",         ulNongbrPdrUe);
        write("DlNongbrDataVolumeUe",  dlNongbrDataVolumeUe);
        write("UlNongbrDataVolumeUe",  ulNongbrDataVolumeUe);
        write("Rsrp",                  rsrp);
    }
};

// Builds the canonical record from one observation as it arrived in a telemetry
// frame. The only place a UserSample is produced.
UserSample makeUserSample(inet::Ptr<const RavensLinkDataFrameMessage> frame,
                          const std::string& userId,
                          const UserData& observation,
                          const std::string& confirmedMEH);

// The CSV header line, built by walking the same field list the rows are
// written from — so the header and the rows cannot disagree about which column
// is which.
std::string userSampleCsvHeader();

// One reading of a host's cell, in the single form every consumer sees. Same
// arrangement as UserSample above, and for the same reason: the CSV columns and
// the JSON keys were two hand-written lists, and they had already drifted — the
// column `CellId` was the key `accessPointId`, `DlPrbUsageCell` was
// `dlTotalPrbUsageCell`, and three columns had no key at all. A model trained on
// a column would have been served a differently named field.
//
// One reading, not one frame: a frame carries the newest reading the RNIS
// reported, or none before it has first replied, and each becomes a row.
struct CellSample
{
    // When the frame carrying this reading left the Agent, and when the RNIS
    // measured it. The second is the one that matters — it is now the time of
    // this reading, rather than the last time a shared record was written to.
    simtime_t frameSentAt;
    simtime_t radioTimestamp;

    std::string observedMEH;
    std::string cellId;

    // Reported by the RNIS for the cell as a whole. -1 means not measured.
    double dlPrbUsageCell = -1.0;
    double ulPrbUsageCell = -1.0;
    double dlNongbrPdrCell = -1.0;
    double ulNongbrPdrCell = -1.0;
    int numActiveUeDlNongbr = -1;

    // Computed by the Agent over the reading's per-UE section. They cover every
    // UE the RNIS reported, while a per-UE sample exists only for UEs the
    // Location Service had already reported — so these span a wider population
    // than the samples do, which is why they are carried rather than recomputed.
    //
    // On a cell with no users the delays are -1 and the volumes 0: a sum over
    // nobody is genuinely zero, a mean over nobody is undefined.
    double avgDlDelay = -1.0;
    double avgUlDelay = -1.0;
    double totalDlDataVolume = -1.0;
    double totalUlDataVolume = -1.0;

    // The one field list, read the same way as UserSample::forEachField.
    template <typename WriteFn>
    void forEachField(WriteFn write) const
    {
        write("TimestampSent",       frameSentAt);
        write("RadioTimestamp",      radioTimestamp);
        write("MEHId",               observedMEH);
        write("CellId",              cellId);
        write("DlPrbUsageCell",      dlPrbUsageCell);
        write("UlPrbUsageCell",      ulPrbUsageCell);
        write("DlNongbrPdrCell",     dlNongbrPdrCell);
        write("UlNongbrPdrCell",     ulNongbrPdrCell);
        write("AvgDlDelay",          avgDlDelay);
        write("AvgUlDelay",          avgUlDelay);
        write("TotalDlDataVolume",   totalDlDataVolume);
        write("TotalUlDataVolume",   totalUlDataVolume);
        write("NumActiveUeDlNongbr", numActiveUeDlNongbr);
    }
};

// Builds the canonical record from one cell reading as it arrived in a telemetry
// frame. The only place a CellSample is produced.
CellSample makeCellSample(inet::Ptr<const RavensLinkDataFrameMessage> frame,
                          const AccessPointRadioInfoData& reading);

// The radio-stats CSV header line, from the same field list as its rows.
std::string cellSampleCsvHeader();

} // namespace simu5g

#endif /* RAVENS_CONTROLLER_APP_TELEMETRYSAMPLE_H_ */
