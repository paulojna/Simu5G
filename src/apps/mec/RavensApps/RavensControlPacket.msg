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

//
// What the RAVENS Controller publishes to the MEC orchestrator: three
// independent streams, one message class each. Which of them a run switches on
// is what defines a profile; see RavensControlProtocol.h for the type values and
// for why the three differ in timing.
//
// None of these are ever transmitted. The Controller -> orchestrator link is a
// direct module connection with no channel, delay or datarate, because the two
// are modelled as co-located management entities that would plausibly share a
// server. Chunk lengths below are set only because a Packet needs one — do not
// add them to a signaling overhead total. Overhead results are scoped to the
// Agent -> Controller path, which does cross the modelled network.
//

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;

cplusplus {{
    #include "RavensControllerApp/DataUpdates/UserEvent.h"
    #include "RavensControllerApp/DataUpdates/MigrationPrediction.h"
    #include "RavensControllerApp/DataUpdates/TelemetrySample.h"

    // These typedefs land in the global namespace, which is shared with every
    // other .msg file in the build — including RavensLinkPacket.msg, which
    // already has a UserSampleList (of UserData) and a CellSampleList (of
    // AccessPointRadioInfoData) for the Agent -> Controller frames. Those names
    // describe the Agent's wire records; these describe the canonical ones, and
    // the Telemetry prefix is what keeps two different meanings of "user sample"
    // from being the same identifier.
    typedef std::vector<::simu5g::MigrationPrediction> MigrationPredictionList;
    typedef std::vector<::simu5g::UserSample> TelemetryUserSampleList;
    typedef std::vector<::simu5g::CellSample> TelemetryCellSampleList;
    typedef std::vector<std::string> MecHostIdList;
}};

class MigrationPredictionList { @existingClass; };
class TelemetryUserSampleList { @existingClass; };
class TelemetryCellSampleList { @existingClass; };
class MecHostIdList { @existingClass; };

namespace simu5g;

// Declared after the namespace, unlike the lists above, because it is the one
// type here that genuinely lives in simu5g — the lists are global typedefs that
// merely hold simu5g elements. Declaring it above would have the message
// compiler generate references to a global ::UserEvent that does not exist.
//
// It also needs the fuller form a struct used as a message *field* requires: the
// compiler cannot introspect a class it did not generate, and @opaque with a
// rendering function is how the rest of this codebase says so (see
// LteCommon.msg). A vector needs none of that, being a shape already understood.
struct UserEvent
{
    @existingClass;
    @opaque;
    @toString(.str());
}

class RavensControlPacket extends inet::FieldsChunk{
    int type;
}

//
// One confirmed change in where a user is. Sent the instant it is confirmed,
// one event per message — not batched, and not accumulated anywhere.
//
class UserEventMessage extends RavensControlPacket
{
    UserEvent event;
}

//
// Expected future moves, sent as the prediction server produces them. Periodic
// only in the sense that the telemetry driving them is: there is no separate
// timer, because every second a prediction waits is a second further ahead the
// model needed to have predicted.
//
class PredictionReportMessage extends RavensControlPacket
{
    MigrationPredictionList predictions;
}

//
// One window of raw observations — the input a reinforcement-learning strategy
// learns from, in the same records the models were trained on.
//
// Unlike the two above, this one waits. The window is one telemetryInterval
// long, which is the period at which each Agent sends exactly one frame, so a
// window holds one complete round of observations regardless of how the Agents'
// schedules are offset from each other. A learning agent wants that coherence
// more than it wants the samples a second sooner.
//
// Expect one row per (user, observing host), which is not the same as one row
// per user. Two hosts' Location Services tick at slightly different moments, so
// a user crossing between them mid-window is observed by both and appears twice,
// with different observedMEH. Those are the most informative rows in the set.
// Telemetry is also loss-tolerant by design, so a user can appear zero times.
//
class TelemetryReportMessage extends RavensControlPacket
{
    // The window this report covers. Sent at windowEnd.
    simtime_t windowStart;
    simtime_t windowEnd;

    // The hosts whose frames actually arrived during the window.
    //
    // Without this, a lost frame and a host with nothing to report are the same
    // message. A host with no users still sends its cell reading, so in steady
    // state its presence here can be inferred from the cell samples — but not
    // before the RNIS has first replied, when a frame legitimately carries
    // neither. Stating it outright saves every consumer from having to know
    // that rule.
    MecHostIdList reportingMEHIds;

    TelemetryUserSampleList userSamples;
    TelemetryCellSampleList cellSamples;
}
