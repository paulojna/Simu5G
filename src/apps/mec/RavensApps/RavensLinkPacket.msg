//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

cplusplus {{
    #include "RavensAgentApp/AccessPointData.h"
    #include "RavensAgentApp/UserData.h"
    #include "RavensAgentApp/AccessPointRadioInfoData.h"
    typedef std::vector<::simu5g::AccessPointData> AccessPointList;
    // The cell's radio state at the tick this frame was built, as the RNIS
    // reported it. A list rather than a single field because it holds either one
    // reading or none: the RNIS has not replied yet in the opening frames of a
    // run, and that absence is worth expressing rather than faking with a
    // sentinel record.
    //
    // Each reading is a whole record with its own timestamp, so it describes
    // one moment rather than being merged into a running value. That is what
    // makes a field that was not measured recognisable: it sits at its "not
    // measured" default in that record instead of holding an older reading.
    typedef std::vector<::simu5g::AccessPointRadioInfoData> CellSampleList;
    // One observation per UE seen at the tick this frame was built. Flat: a
    // frame carries at most one reading per UE, and UserData holds its own
    // address, so there is nothing for a per-UE wrapper to save.
    typedef std::vector<::simu5g::UserData> UserSampleList;
    struct RavensEvent {
         std::string ueAddress;
         int eventType;           // EVENT_ENTRY=0 or EVENT_EXIT=1
         int samplesSinceChange;  // consecutive 1s LS samples in this state
         omnetpp::simtime_t firstDetectedAt;
    };
    typedef std::vector<RavensEvent> RavensEventList;
}};

class AccessPointList { @existingClass; };
class UserSampleList { @existingClass; };
class CellSampleList { @existingClass; };
class RavensEventList { @existingClass; };

namespace simu5g;

class RavensLinkPacket extends inet::FieldsChunk{
    int type;
    int requestId;
    simtime_t timeStamp;
}

class RavensLinkJoinNetworkRequestMessage extends RavensLinkPacket
{
	string mecHostId;
}

class RavensLinkInfrastructureDetailsMessage extends RavensLinkPacket
{
    string mecHostId;
    AccessPointList APList;
}

class RavensLinkInfrastructureDetailsMessageAck extends RavensLinkPacket
{
    int infoType;
    int telemetryIntervalMs;  // how often the Agent sends telemetry frames, in ms
    int agentMode;  // EVENT_ONLY_MODE=0, TELEMETRY_ONLY_MODE=1 (not yet wired), FULL_MODE=2
}

// Periodic telemetry frame (TELEMETRY_FRAME): a snapshot of the newest tick —
// one Location Service reading for each UE seen at that tick, plus the cell's
// newest RNIS reading. A frame describes the instant it left, not the interval
// before it: readings taken between frames are not carried.
//
// One reading per frame is what keeps a coarser interval reproducible offline.
// Subsampling a one-second corpus to every third row yields exactly what a
// three-second frame carries, so a model trained on coarser data matches what a
// coarser frame serves, and every coarser dataset can be derived from a single
// generation pass at one second.
//
// Absence from a frame is a gap in observation and nothing more. It never means
// departure — that stays owned by the event channel. A UE seen between frames
// but gone by the send tick is simply not reported, and a departing UE's final
// approach to the cell boundary survives only if it was still present at that
// tick. At a one-second interval neither costs anything, since every tick is a
// frame.
//
// Only sent when agentMode = FULL_MODE (or TELEMETRY_ONLY_MODE, once wired).
// Transport: UDP telemetry channel — loss-tolerant.
class RavensLinkDataFrameMessage extends RavensLinkPacket
{
    string mecHostId;
    UserSampleList userSamples;
    CellSampleList cellSamples;
}

// Event frame (EVENT_FRAME): sent when the Agent confirms a UE entering or
// leaving this host, at the Location Service tick where the confirmation
// happens — not on any frame timer. Confirming takes several consecutive
// samples agreeing, so a UE that only brushes past this host is never
// reported at all.
//
// One frame carries every change confirmed at the same tick, so several UEs
// crossing together cost one packet rather than several. If no change was
// confirmed, nothing is sent. Always sent regardless of agentMode.
//
// Transport: TCP signaling channel (report-once semantics need reliable
// delivery), kept open after the config handshake. If that channel is down,
// the events are held and retried at the next tick.
class RavensLinkEventMessage extends RavensLinkPacket
{
    string mecHostId;
    RavensEventList events;  // all state changes this frame interval
}
