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
    // One reading of the cell's radio state, as the RNIS reported it. The
    // subscription runs once a second while telemetry frames leave less often,
    // so a frame normally carries several, oldest first.
    //
    // Each reading is a whole record with its own timestamp, so it describes
    // one moment rather than being merged into a running value. That is what
    // makes a field that was not measured recognisable: it sits at its "not
    // measured" default in that record instead of holding an older reading.
    typedef std::vector<::simu5g::AccessPointRadioInfoData> CellSampleList;
    // One UE's observations since the previous telemetry frame. The Location
    // Service ticks once a second while telemetry frames go out less often, so
    // a group normally holds several samples, oldest first.
    //
    // Grouping is done by the Agent because the Agent is the only place the
    // per-UE sequence naturally exists: it is what observed the UE, tick by
    // tick. Every consumer downstream reads per-UE trajectories, so a flat
    // list would mean each of them rebuilding a grouping the sender already
    // had and discarded.
    struct UeSampleGroup {
        std::string ueAddress;
        std::vector<::simu5g::UserData> samples;
    };
    typedef std::vector<UeSampleGroup> UeSampleGroupList;
    struct RavensEvent {
         std::string ueAddress;
         int eventType;           // EVENT_ENTRY=0 or EVENT_EXIT=1
         int samplesSinceChange;  // consecutive 1s LS samples in this state
         omnetpp::simtime_t firstDetectedAt;
    };
    typedef std::vector<RavensEvent> RavensEventList;
}};

class AccessPointList { @existingClass; };
class UeSampleGroupList { @existingClass; };
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

// Periodic telemetry frame (TELEMETRY_FRAME): everything observed since the
// previous frame — every Location Service reading, grouped per UE, and every
// RNIS reading of the cell. The frame cadence and the sensing rate are
// deliberately different: the Agent observes every second and reports less
// often, sending everything it saw in between rather than only the newest
// value. Both halves of the frame follow that rule, so a frame is a record of
// an interval rather than a snapshot of the instant it left.
//
// A UE that left partway through the interval still has its buffered samples
// delivered here: the samples were captured when observed, so the UE's
// departure from the Agent's current-state map does not discard them. Those
// final observations are the approach to a cell boundary, which is exactly what
// the handover models need.
//
// Only sent when agentMode = FULL_MODE (or TELEMETRY_ONLY_MODE, once wired).
// Transport: UDP telemetry channel — loss-tolerant.
class RavensLinkDataFrameMessage extends RavensLinkPacket
{
    string mecHostId;
    UeSampleGroupList userSamples;
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
