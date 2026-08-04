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

// One UE's group in a telemetry frame. The UE is identified once, and its
// observations follow. Sending the identity per group rather than per sample is
// the only size saving grouping buys - small, but it is the honest encoding: a
// real protocol would not repeat the address on every observation of the same
// UE.
//   address 4 + sample count 1 (+3 padding)
const int UE_GROUP_HEADER_B = 8;

// One observation of one UE - a single Location Service tick, with whatever the
// RNIS had reported for that UE at the time. Two halves, one per data source:
//   Location Service 32: cell id 4, x/y/z 12, bearing 2, speed 2,
//                        distance to AP 4, timestamp 8
//   RNIS             34: dl/ul delay 8, dl/ul PDR 8, dl/ul data volume 8,
//                        RSRP 2, timestamp 8
// 66 bytes of fields, padded to 72 for 8-byte alignment. No address: it lives
// in the group header above.
//
// Both halves are always present: when the RNIS has no value it reports -1
// rather than omitting the field, so there is no shorter variant of this record.
// Each half carries its own timestamp because the two services sample on
// independent cycles - a repeated radio value is identifiable by its unchanged
// timestamp rather than being mistaken for a fresh measurement.
const int UE_SAMPLE_B = 72;

// Sanity check on the split: a group holding exactly one sample costs
// 8 + 72 = 80 bytes, which is precisely what one user cost before batching.
// The change decomposes that number into "identity once, observation each" -
// it does not re-base it, so earlier size figures remain comparable.

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
// roughly 19 UEs when each carries three observations.
const int TELEMETRY_PATH_MTU_B = 4470;

// ---------------------------------------------------------------------------
// Frame sizing
// ---------------------------------------------------------------------------
// Each function takes the payload that is about to be sent, so the call site
// reads as "size this message" instead of repeating arithmetic. For a
// fixed-width encoding this is the closed form of walking every field, so it
// returns the same number - the point is that adding a field to UserData or
// RavensEvent is a one-place update here, not a silent mismatch.

// Telemetry frame: one group per UE observed since the last frame, each holding
// that UE's observations, plus the cell aggregates if the Agent has heard from
// the RNIS yet (it has not, before the first RNIS reply).
//
// Groups are walked rather than multiplied out because they do not all hold the
// same number of samples: a UE that arrived or left partway through the interval
// contributes fewer than one that was present throughout.
inline inet::B telemetryFrameBytes(const ::UeSampleGroupList& groups, bool hasCellRadio)
{
    int payloadBytes = 0;
    for (const auto& group : groups)
        payloadBytes += UE_GROUP_HEADER_B + (int)group.samples.size() * UE_SAMPLE_B;

    return inet::B(FRAME_HEADER_B
                   + payloadBytes
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
