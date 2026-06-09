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
    struct RavensControlEvent {
         std::string ueAddress;
         int eventType;
         int samplesSinceChange;
         omnetpp::simtime_t firstDetectedAt;
    };
    typedef std::vector<RavensControlEvent> RavensControlEventList;
}};

class AccessPointList { @existingClass; };
class UsersMap { @existingClass; };
class AccessPointRNISData { @existingClass; };
class RavensControlEventList { @existingClass; };

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
    int rate;       // data frame interval in ms
    int agentMode;  // AGENT_MODE_CONTROL_ONLY=0 or AGENT_MODE_CONTROL_AND_DATA=1
}

// Periodic data frame: LS-derived user map + RNIS-derived cell aggregates.
// Only sent when agentMode = AGENT_MODE_CONTROL_AND_DATA.
class RavensLinkDataFrameMessage extends RavensLinkPacket
{
    string mecHostId;
    UsersMap users;
    AccessPointRNISData apRadioInfo;
}

// Conditional control frame: sent at frameInterval only when user state changes
// exist (entries or exits). If no changes occurred, no packet is sent.
// Carries all state changes since the previous control frame as a batch.
// Always sent regardless of agentMode.
class RavensLinkControlEventMessage extends RavensLinkPacket
{
    string mecHostId;
    RavensControlEventList events;  // all state changes this frame interval
}
