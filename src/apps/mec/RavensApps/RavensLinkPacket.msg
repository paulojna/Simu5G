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
    typedef std::unordered_map<std::string, ::simu5g::UserData> UsersMap;
    typedef simu5g::AccessPointRadioInfoData AccessPointRNISData;
    struct RavensEvent {
         std::string ueAddress;
         int eventType;           // EVENT_ENTRY=0 or EVENT_EXIT=1
         int samplesSinceChange;  // consecutive 1s LS samples in this state
         omnetpp::simtime_t firstDetectedAt;
    };
    typedef std::vector<RavensEvent> RavensEventList;
}};

class AccessPointList { @existingClass; };
class UsersMap { @existingClass; };
class AccessPointRNISData { @existingClass; };
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

// Periodic telemetry frame (TELEMETRY_FRAME): LS-derived user map + RNIS-derived
// cell aggregates. Only sent when agentMode = FULL_MODE (or TELEMETRY_ONLY_MODE,
// once wired). Transport: UDP telemetry channel — loss-tolerant, superseded by
// the next frame.
class RavensLinkDataFrameMessage extends RavensLinkPacket
{
    string mecHostId;
    UsersMap users;
    AccessPointRNISData apRadioInfo;
}

// Event frame (EVENT_FRAME): sent only when UE state changes exist (entries or
// exits). If no changes occurred, no packet is sent. Carries all state changes
// since the previous event frame as a batch. Always sent regardless of
// agentMode. Transport: TCP signaling channel (report-once semantics need
// reliable delivery), kept open after the config handshake.
// Note: still emitted on the telemetry timer today; a later step moves it to
// fire the moment a change is confirmed.
class RavensLinkEventMessage extends RavensLinkPacket
{
    string mecHostId;
    RavensEventList events;  // all state changes this frame interval
}
