#ifndef _APPS_MEC_RAVENSAPPS_RAVENSLINKSIZES_H_
#define _APPS_MEC_RAVENSAPPS_RAVENSLINKSIZES_H_

#include "inet/common/INETDefs.h"
#include "RavensLinkPacket_m.h"

// Wire sizes for the RavensLink protocol (Agent <-> Controller), shared by both
// ends so the two sides cannot declare different sizes for the same frame.
//
// WHY THIS FILE EXISTS
// Every RAVENS frame used to declare a hardcoded 500 bytes, so a frame carrying
// 2 users and one carrying 40 claimed the same size. Any statement about
// signaling cost was therefore unmeasurable. Here, size follows content.
//
// WHAT THE NUMBERS MEAN
// They describe a fixed-width binary encoding that a real deployment would use
// - IPv4 address in 4 bytes, cell id in 4, a metric in 2 or 4. They are NOT the
// size of the C++ objects, which carry std::string and unordered_map overhead
// that no protocol would put on a wire. Deliberate: encoding the strings
// verbatim would make frame size depend on how long OMNeT++ module names
// happen to be, which is a simulator artifact, not a property of the protocol.
//
// These sizes are what the IP layer reports, because INET derives packet bytes
// from the chunk length set here. Nothing measures them independently, so they
// define the measurement rather than approximate it. Keep the field lists in
// the comments below accurate - they are the only justification the numbers
// have.

namespace simu5g {

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

// Common header on every Agent -> Controller frame:
//   type 4 + requestId 4 + timestamp 8 + mecHostId 8
const int FRAME_HEADER_B = 24;

// Controller -> Agent replies (join ack, config ack). Same header minus the
// host id: the Agent already knows which host it is, so the Controller does not
// send it back.
//   type 4 + requestId 4 + timestamp 8
const int ACK_HEADER_B = 16;

// ---------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------

// One observation of one UE - a single Location Service tick, with whatever the
// RNIS had reported for that UE at the time. Three parts:
//   address           4
//   Location Service 32: cell id 4, x/y/z 12, bearing 2, speed 2,
//                        distance to AP 4, timestamp 8
//   RNIS             34: dl/ul delay 8, dl/ul PDR 8, dl/ul data volume 8,
//                        RSRP 2, timestamp 8
// 70 bytes of fields, padded to 72 for 8-byte alignment.
//
// The address travels in every record because a frame carries at most one
// observation per UE, so there is no repetition for a per-UE wrapper to remove.
// It costs nothing here either - 66 bytes of measurements padded to 72 leaves
// exactly the room the address needs.
//
// Both halves are always present: when the RNIS has no value it reports -1
// rather than omitting the field, so there is no shorter variant of this record.
// Each half carries its own timestamp because the two services sample on
// independent cycles - a repeated radio value is identifiable by its unchanged
// timestamp rather than being mistaken for a fresh measurement.
const int UE_SAMPLE_B = 72;

// One reading of the cell's radio state. One per frame, not one per user - one
// Agent serves one cell, so a reading is shared by every UE in the frame rather
// than repeated for each of them. A frame carries the newest reading, or none
// at all before the RNIS has first replied.
//   cell id 4 + timestamp 8 + 9 metrics 36:
//     dl/ul PRB usage 8, dl/ul non-GBR PDR 8, active UE count 4,
//     dl/ul mean delay 8, dl/ul total data volume 8
// 48 bytes, already 8-byte aligned.
//
// The previous value was 64 for "10 metrics", which did not add up either way -
// 4 + 8 + 40 is 52. One of those ten was the mean distance to the access point,
// a Location Service quantity that has been removed from this record; the
// remaining nine are counted above, field by field.
const int CELL_RADIO_RECORD_B = 48;

// One access point in the infrastructure-details frame, sent once at handshake.
//   AP id 4 + x/y/z 12
const int AP_RECORD_B = 16;

// One ENTRY or EXIT in an event frame. Small on purpose: this is the control
// plane, and its cheapness against periodic polling is the efficiency claim.
//   address 4 + event type 1 + sample count 1 + firstDetectedAt 8 (+2 padding)
const int EVENT_RECORD_B = 16;

// Payload of the config ack, which tells the Agent how to run:
//   infoType 4 + telemetry interval 4 + agent mode 4
const int CONFIG_ACK_PAYLOAD_B = 12;

// ---------------------------------------------------------------------------
// Path limit
// ---------------------------------------------------------------------------

// Largest datagram that crosses the Agent -> Controller path without being
// split up. It is 4470 and not the familiar 1500 because that path runs over
// point-to-point interfaces, whose default limit in INET is 4470 (Ppp.ned).
// "Eth10G" on those links names a datarate channel, not an Ethernet interface,
// so the Ethernet limit never applies here.
//
// Exceeding it is allowed and safe: the network layer splits the datagram and
// the receiver puts it back together, and this path - 10 Gbps, no bit errors,
// no realistic congestion - gives no reason to expect a piece to go missing.
// The Agent warns when a frame crosses this line purely so it is known how
// often busy hosts do it, not to prevent it. A frame stays under the line up to
// 60 UEs:
//   4470 - 28 UDP and IP headers - 24 frame header - 48 cell reading = 4370
//   4370 / 72 per UE = 60 UEs
//
// That headroom is a property of this path, not of a deployment. Over Ethernet
// the limit is 1500, which leaves room for 19.
const int TELEMETRY_PATH_MTU_B = 4470;

// ---------------------------------------------------------------------------
// Frame sizing
// ---------------------------------------------------------------------------
// Each function takes the payload that is about to be sent, so the call site
// reads as "size this message" instead of repeating arithmetic. For a
// fixed-width encoding this is the closed form of walking every field, so it
// returns the same number - the point is that adding a field to UserData or
// RavensEvent is a one-place update here, not a silent mismatch.

// Telemetry frame: one record per UE observed at this tick, plus the cell's
// newest radio reading. Both counts are what was actually collected, so a frame
// sent before the first RNIS reply honestly carries no cell record at all, and
// a tick on which the Location Service reported nobody carries no UE records.
inline inet::B telemetryFrameBytes(int userSampleCount, int cellSampleCount)
{
    return inet::B(FRAME_HEADER_B
                   + userSampleCount * UE_SAMPLE_B
                   + cellSampleCount * CELL_RADIO_RECORD_B);
}

// Event frame: only the state changes being reported. Never sent empty.
inline inet::B eventFrameBytes(const ::RavensEventList& events)
{
    return inet::B(FRAME_HEADER_B + (int)events.size() * EVENT_RECORD_B);
}

// Infrastructure details: the Agent's access point list, sent once at handshake.
inline inet::B infrastructureFrameBytes(const ::AccessPointList& aps)
{
    return inet::B(FRAME_HEADER_B + (int)aps.size() * AP_RECORD_B);
}

// Join request: header only. The mecHostId it carries is already counted there.
inline inet::B joinRequestBytes()
{
    return inet::B(FRAME_HEADER_B);
}

// Join ack: header only, no payload.
inline inet::B joinAckBytes()
{
    return inet::B(ACK_HEADER_B);
}

// Config ack: the Agent's operating parameters.
inline inet::B configAckBytes()
{
    return inet::B(ACK_HEADER_B + CONFIG_ACK_PAYLOAD_B);
}

} //namespace

#endif
