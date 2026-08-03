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

// One UE in a telemetry frame. Two halves, one per data source:
//   Location Service 40: address 4, cell id 4, x/y/z 12, bearing 2, speed 2,
//                        distance to AP 4, timestamp 8 (+2 padding)
//   RNIS             40: dl/ul delay 8, dl/ul PDR 8, dl/ul data volume 8,
//                        RSRP 2, timestamp 8 (+6 padding)
// Both halves are always present: when the RNIS has no value it reports -1
// rather than omitting the field, so there is no shorter variant of this record.
const int USER_RECORD_B = 80;

// Cell-level radio aggregates in a telemetry frame. Sent ONCE per frame, not
// once per user - one Agent serves one cell, so these values are shared by
// every UE in the frame.
//   cell id 4 + timestamp 8 + 10 aggregate metrics (4 each, 2 of them counts)
const int CELL_RADIO_RECORD_B = 64;

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
// Frame sizing
// ---------------------------------------------------------------------------
// Each function takes the payload that is about to be sent, so the call site
// reads as "size this message" instead of repeating arithmetic. For a
// fixed-width encoding this is the closed form of walking every field, so it
// returns the same number - the point is that adding a field to UserData or
// RavensEvent is a one-place update here, not a silent mismatch.

// Telemetry frame: the per-UE records, plus the cell aggregates if the Agent
// has heard from the RNIS yet (it has not, before the first RNIS reply).
inline inet::B telemetryFrameBytes(const ::UsersMap& users, bool hasCellRadio)
{
    return inet::B(FRAME_HEADER_B
                   + (int)users.size() * USER_RECORD_B
                   + (hasCellRadio ? CELL_RADIO_RECORD_B : 0));
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
