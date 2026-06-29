# eRAVENS Implementation Plan
## efficient RAVENS — two-frame architecture

---

## 0. Motivation & Development Considerations

> Read this first. It explains *why* eRAVENS exists and the principles to keep in
> mind while finishing it. Everything below Section 0 is the *how*.

### 0.1 What RAVENS is

RAVENS is a distributed MEC orchestration *sensing + decision* layer. Each MEC
host runs a **RavensAgentApp** that discovers its host's cells, subscribes to the
local **Location Service (LS)** and **Radio Network Information Service (RNIS)**,
and reports to a single global **RavensControllerApp**. The Controller maintains
the global picture (which UE is on which MEC host, plus per-cell radio health)
and drives a **MEO (Mobile Edge Orchestrator)**: instantiate an app on ENTRY,
tear it down on EXIT, migrate it on handover. Three Controller "handler policies"
implement three strategies — `SaveDataHistory` (dataset logging),
`NotifyOnDataChange` (reactive), `SendToExternalServer` (proactive ML via Flask).

### 0.2 Why eRAVENS — the problems with "normal" RAVENS

The original RAVENS uses a **single periodic snapshot**: every Agent sends its
full user map + radio data to the Controller on a fixed cadence, and the
Controller infers everything (entries, exits, handovers) by *diffing successive
snapshots* and applying time-based heuristics. This created four structural
problems:

1. **Hidden timing constraint.** Correct behaviour silently depended on
   `ttl_ + forceUpdateInterval_ < HANDOVER_LOCKOUT` — three values set in three
   different files, with nothing enforcing the relationship. Change one and
   handovers break in non-obvious ways.
2. **Stale data.** TTL purging ran only at snapshot time, so a departed user
   could linger in the global view for up to ~10 s.
3. **LS/RNIS coupling.** Per-user LS and per-user RNIS were merged at the user
   level even though they arrive on different schedules and the per-user RNIS L2
   delay is weakly informative for handover. A `-1` sentinel was used to paper
   over the de-sync.
4. **No traffic separation, no efficiency story.** One frame type served all
   purposes, full snapshots were sent even when nothing changed, and there was no
   way to *measure* the cost of the system — which a PhD comparison needs.

### 0.3 The eRAVENS idea

Split Agent→Controller traffic into **two frame types** and push intelligence to
the MEC host (the "e" = *efficient*):

- **Event frames** — *what changed*: UE ENTRY/EXIT events, derived by the Agent
  diffing the LS user list at 1 s granularity, annotated with a confidence
  signal (`samplesSinceChange`). Sent only when something changes.
- **Data frames** — *features*: LS-derived user state + **cell-level aggregated**
  RNIS (not per-user). Periodic, only in modes that need it.

Both are **data-plane** traffic (telemetry the Agent reports). Distinct from these
is the **control plane** — the config handshake (JOIN / INFRAESTRUCTURE_DETAILS /
ACKs) that sets `agentMode` + `frameInterval`. See **Revision R1** for the
terminology and the TCP-vs-UDP transport split.

This (a) lets the Agent use MEC-host compute to do the diff/aggregation locally,
reducing backhaul; (b) removes the hidden timing constraint by making departures
event-driven; (c) makes the system **measurable** (bytes per frame type), which
enables the three-way research comparison — reactive only, proactive only,
reactive + proactive.

**Why this is a legitimate MEC contribution:** LS and RNIS are MEC *platform
services*, reachable only from inside the MEC host. The diff, the sample
counting, and the RNIS aggregation *must* run at the edge — a cloud controller
cannot subscribe to these. eRAVENS is therefore a concrete exploitation of MEC
host capability to cut network cost, which is exactly the kind of claim the
thesis needs to defend.

### 0.4 Considerations when continuing development

These are the principles that should govern every remaining decision:

1. **LS is the single source of truth for presence.** RNIS describes *cell
   health*, never *who is where*. Do not reintroduce per-user RNIS as a presence
   signal. (The radio layer guarantees a UE is in exactly one cell at a time, so
   LS presence is unambiguous.)
2. **Agent reports facts; Controller owns policy.** The Agent counts samples and
   reports deltas. All accept/reject/merge *decisions* (handover confirmation,
   departure-vs-handover) live in the Controller. Keep this separation — it is
   what makes the timing configurable in one place.
3. **Don't trade one hidden constraint for another.** The whole point was to kill
   `ttl + forceUpdate < HANDOVER_LOCKOUT`. Any new timing parameter (e.g. the
   exit-hold window in F2, or `threshold_` vs `frameInterval`) must be *local,
   documented, and ideally derived* from `frameInterval` rather than set
   independently. See the Logic Faults section.
4. **The delta model breaks "periodic full state" assumptions.** Because stable
   users are no longer re-sent every cycle, any Controller mechanism that assumed
   periodic refresh (inactivity purging, confirmation-across-frames) must be
   rethought, not ported. This is the root of critique items C1–C3.
5. **Preserve the three-config experiment.** Reactive-only / proactive-only /
   both must all be *expressible and runnable* — the mode/flag design must allow
   "data without events" (see C3). The comparison is the deliverable.
6. **Implement piece by piece, review before applying.** The codebase is mid-
   migration and currently does not compile by design; follow the Pieces in
   order and keep the status snapshot at the end of this file current.
7. **Defer cosmetic churn.** Class renames (`UserData`→`UEState`, etc.) and the
   `INFRAESTRUCTURE` typo are real but high-blast-radius; batch them into the
   final cleanup pass (Piece 12), not mid-flight.

---

## Context

eRAVENS is a redesign of the RAVENS Agent-Controller system. The core
motivation is to **leverage MEH (MEC Host) computational capabilities** to
reduce network traffic, eliminate hidden timing constraints, and produce
a cleaner architecture that supports proactive ML-based handover prediction
alongside reactive event-driven detection.

---

## Problem with current RAVENS

| Problem | Root cause |
|---|---|
| Stale users in snapshots for up to 10s | TTL purge only runs at snapshot time, not on LS notification |
| Hidden timing constraint (`ttl_ + forceUpdate < HANDOVER_LOCKOUT`) | Three values set in three different places with no enforcement |
| Per-user RNIS fields delayed and out of sync with LS | Two independent data sources merged at user level |
| Rate negotiation broken | `rate=3000` hardcoded on Controller, integer division on Agent |
| Handover lockout hardcoded in two separate methods | `HANDOVER_LOCKOUT = 10.0` appears as local const twice |
| Agent sends full snapshots even when nothing changed | Dirty flag + force-heartbeat overlap creates non-obvious effective rate |
| No distinction between event and bulk-data traffic | Single frame type serves all purposes |
| Config handshake unreliable | JOIN/DETAILS/ACKs sent over UDP — a dropped ACK leaves the Agent in default mode (fixed in R1: handshake moves to TCP) |

---

## eRAVENS Architecture

### MEC justification

eRAVENS deliberately places intelligence at the MEH layer. LS and RNIS are
**MEC platform services** — only accessible from within the MEC host. No
centralized controller can subscribe to them directly. This means the
computation must live at the edge.

What the Agent does locally (MEH compute):
1. **LS diff at 1s granularity** — tracks user entry, exit, and consecutive
   sample counts. Only state changes leave the MEH, not the full user list.
2. **Temporal aggregation** — 3× 1s LS samples collapsed into one enriched
   event frame at `frameInterval` (e.g. 3s), with confidence context.
3. **RNIS aggregation** — per-user radio stats (N users × 6 fields) aggregated
   to cell-level metrics (6 fields total) before transmission.

Result: Agent→Controller bandwidth is a fraction of naive forwarding.
The `SaveDataHistory` / `SendToExternalServer` modes log and measure this,
enabling quantitative comparison across experimental configurations.

---

### Two frame types (both data plane, both over UDP)

**Event frame** (`UE_EVENT`) — periodic, confidence-annotated:
- Sent every `frameInterval` (e.g. 3s), always, regardless of agent mode
- Carries a **batch** of user state changes since last frame, each annotated with:
  - `eventType`: ENTRY or EXIT
  - `samplesSinceChange`: how many consecutive 1s LS samples the user has been
    in this state (e.g. present for 3 samples = confident; absent for 1 = uncertain)
  - `firstDetectedAt`: simtime when the state change was first seen
- Only changed users are reported — stable users are omitted from event frames
- The Controller uses `samplesSinceChange` to make handover/departure decisions
  without needing its own per-user history or a separate exitTTL timer

**Data frame** (`DATA_FRAME`) — periodic, full state:
- Sent every `frameInterval`, only when `agentMode = AGENT_MODE_EVENT_AND_DATA`
- LS-derived user map: address, AP, location, speed, bearing, distance
- RNIS-derived cell aggregates: PRB usage, avg delay, PDR, data volume, avg distance
- No per-user RNIS fields

### Agent mode (set by Controller via INFRAESTRUCTURE_DETAILS_ACK)

| Mode | Agent sends | Used by |
|---|---|---|
| `AGENT_MODE_EVENT_ONLY (0)` | Event frames only | NotifyOnDataChange |
| `AGENT_MODE_EVENT_AND_DATA (1)` | Event + Data frames | SaveDataHistory, SendToExternalServer |

### Registration flow (config handshake over TCP — see R1)

The handshake (boxed below) is the **control plane** and runs over a **TCP**
connection the Agent opens to the Controller's control port; the Agent closes it
after the second ACK. The operational frames are **data plane** over **UDP**.

```
Agent                          Controller
  |  ===== control plane: TCP =====|
  |--- JOIN_NETWORK_REQUEST ------>|
  |<-- JOIN_NETWORK_ACK -----------|
  |--- INFRAESTRUCTURE_DETAILS --->|  (AP list)
  |<-- INFRAESTRUCTURE_DETAILS_ACK-|  (agentMode + frameInterval in ms)
  |  [Agent closes TCP control conn]
  |                                |
  |--- [subscribes to LS + RNIS] --|  (internal, no Controller involvement)
  |                                |
  |  ===== data plane: UDP ========|
  |--- UE_EVENT ----------------->|  (every frameInterval, all modes)
  |--- DATA_FRAME --------------->|  (every frameInterval, mode=1 only)
```

### Agent internal LS tracking (between frame sends)

The Agent sees LS notifications every 1s. Between event frame sends it maintains
two maps of `PendingEvent{firstDetectedAt, sampleCount}` (see Piece 6):
- `pendingEntries_` — users that appeared since the last frame, `sampleCount` =
  consecutive samples present
- `pendingExits_` — users absent from LS, `sampleCount` = consecutive samples absent
- On each LS notification:
  - Users present: increment the entry's `sampleCount`; cancel any pending exit
  - Users absent (were in map before): move to `pendingExits_`, increment its `sampleCount`
- At event frame time: report all entries and all pending exits with their
  `sampleCount` (→ `samplesSinceChange`). Clear both maps after reporting.

### Handover detection (Controller)

> **Updated per D1.** The Controller does **not** threshold `samplesSinceChange`.
> Presence is unconditional (LS is truth); the only debounce is the F2 exit-hold.

```
Event frame from MEH-B: user ENTRY (any samplesSinceChange)
  → user already known at MEH-A → HANDOVER (handled via the hold; both arrival
    orders converge to one handover hook). Brand-new user → ENTRY, created at once.

Event frame from MEH-A: user EXIT (any samplesSinceChange)
  → if source == user's current MEH, open an F2 hold (≈1 frameInterval):
      ENTRY elsewhere within the window → reclassify as handover (hold cancelled)
      re-ENTRY at same MEH within the window → flap (hold cancelled, no-op)
      window elapses with no re-entry → DEPARTURE (onUserExit)
  → if source != current MEH → stale EXIT, ignored (C5)

No agent reports user for threshold_ seconds (EVENT_AND_DATA only):
  → removeInactiveUsers() → EXIT  (lost-packet safety net; see C1/L7)
```

`samplesSinceChange` replaces the old `exitTTL` *concept* but is now logged
metadata, not a control input — the F2 hold (length `frameInterval`) is the timer.

### Research comparison

eRAVENS supports three experimental configurations:

1. **Reactive only** — `NotifyOnDataChange` mode, event frames only
   - Handover detected within `frameInterval` (1–5s configurable)
   - Always correct, always after the fact
   - Minimum bandwidth

2. **Proactive only** — `SendToExternalServer` mode, data frames only (no events)
   - ML predicts handovers before they happen using cell-level aggregates + LS state
   - May miss handovers, no safety net

3. **Reactive + Proactive** — `SendToExternalServer` mode, event + data frames
   - ML predicts early, event frames catch what ML misses
   - Best latency, zero missed handovers

The bandwidth consumed by each configuration is measured via OMNeT++ signals
(bytes per frame type per simulation second), enabling direct quantitative
comparison.

---

## Implementation Plan

> **Read "Revision R1" (below the Pieces) first.** It renames the "control frame"
> → "event frame" and moves the config handshake to TCP. Every Piece below is
> shown in **post-R1 (target) vocabulary**. Pieces 1–6 were already implemented
> under the *old* names (`UE_CONTROL_EVENT`, `AGENT_MODE_CONTROL_*`, etc.); the
> rename to the names shown here is the outstanding R1a work.

### ✅ Piece 1 — Defines (`RavensAgentApp.h`, `RavensControllerApp.h`)

**Status: DONE (under old names; R1a rename outstanding)**

Changed in both files:
- Removed `SET_RETRIEVAL_INTERVAL (4)` and `SET_RETRIEVAL_INTERVAL_ACK (5)` (unused)
- Renamed `USERS_INFO_SNAPSHOT (6)` → `DATA_FRAME (6)`
- Added `UE_EVENT (7)` — *R1a: give it a fresh value to clear the `7` collision
  with the MEO-facing `USERS_UPDATE`; renumber MEO codes to 20/21 (see F3)*
- Added `AGENT_MODE_EVENT_ONLY (0)` and `AGENT_MODE_EVENT_AND_DATA (1)`
- Added `EVENT_ENTRY (0)` and `EVENT_EXIT (1)`
- Removed `CHANGE_ENTRY/MEH/POSITION/EXIT/NO_CHANGE` from Controller header
  (internal state labels, belong in handler logic not wire protocol)

---

### ✅ Piece 2 — Message classes (`RavensLinkPacket.msg`)

**Status: DONE (under old names; R1a rename outstanding)**

- Renamed `RavensLinkUsersInfoSnapshotMessage` → `RavensLinkDataFrameMessage`
  - Fields unchanged: `mecHostId`, `UsersMap users`, `AccessPointRNISData apRadioInfo`
- Updated `RavensLinkInfrastructureDetailsMessageAck`:
  - Kept `int rate` (frame interval in ms — same rate for event and data frames)
  - Added `int agentMode` (AGENT_MODE_EVENT_ONLY or AGENT_MODE_EVENT_AND_DATA)
- Added `RavensEvent` struct and `RavensEventList` typedef in
  the `cplusplus {{ }}` block (consistent with `@existingClass` pattern):
  ```cpp
  struct RavensEvent {
      std::string        ueAddress;
      int                eventType;           // EVENT_ENTRY=0 or EVENT_EXIT=1
      int                samplesSinceChange;  // consecutive 1s LS samples in this state
      omnetpp::simtime_t firstDetectedAt;     // simtime when change was first observed
  };
  typedef std::vector<RavensEvent> RavensEventList;
  ```
  Note: `RavensEvent` / `RavensEventList` are in the **global
  namespace** (not `simu5g::`) — reference without namespace prefix in C++ code.
- Added `RavensLinkEventMessage extends RavensLinkPacket`:
  ```
  string mecHostId
  RavensEventList events   // batch: all state changes this frame interval
  ```
  Sent at `frameInterval` **only when entries or exits exist**. No packet if
  no changes occurred. `exitTTL` is NOT in the message — the Controller uses
  `samplesSinceChange` instead of a local timer.

  Transport note (R1): this message and `RavensLinkDataFrameMessage` go over
  **UDP**; only the JOIN/INFRAESTRUCTURE handshake messages move to **TCP**.

---

### ✅ Piece 3 — `UserData` (`UserData.h`, `UserData.cc`)

**Status: DONE**

Removed all 6 per-user RNIS fields and `rnisUpdate` timestamp, plus all their
getters/setters. Consolidated `lastUpdated` and `lsUpdate` into a single
`timestamp` field (set when user first appears, named consistently with
`AccessPointRadioInfoData`).

`UserData` is now pure LS state:
```
address, accessPointId, currentLocation, distance_to_ap, timestamp
```

---

### ✅ Piece 4 — `AccessPointRadioInfoData` (`AccessPointRadioInfoData.h`, `.cc`)

**Status: DONE**

Added `timestamp` field (consistent naming with `UserData`). Removed parameterized
constructor (was incomplete with new fields). All fields now initialized to 0.

Final field set:
```cpp
// Identity + timing
std::string        accessPointId;
omnetpp::simtime_t timestamp;

// From RNIS cellInfo (authoritative cell-level aggregates)
double dl_total_prb_usage_cell;
double ul_total_prb_usage_cell;
double dl_nongbr_pdr_cell;
double ul_nongbr_pdr_cell;
int    number_of_active_ue_dl_nongbr_cell;  // RNIS already counts active UEs

// Computed from RNIS cellUEInfo (aggregated at Agent)
double avg_dl_delay;
double avg_ul_delay;
double total_dl_data_volume;
double total_ul_data_volume;

// Computed from LS user map at data frame send time
double avg_distance_to_ap;
```

---

### ✅ Piece 5 — `RavensAgentApp.ned`

**Status: DONE**

Replaced `double ttl @unit(s) = default(5s)` with
`double frameInterval @unit(s) = default(3s)`.
Overridden at runtime by `rate` field in `INFRAESTRUCTURE_DETAILS_ACK`.

---

### ✅ Piece 6 — `RavensAgentApp.h`

**Status: DONE (under old names; R1a rename outstanding)**

Removed: `sendInterval`, `forceUpdateInterval_`, `lastSentTimestamp_`, `ttl_`,
`hasPendingUpdates_`, `getRetrievalInterval()`, `setRetrievalInterval()`.

Added:
```cpp
simtime_t frameInterval_;   // negotiated with Controller, used for both frame types
int agentMode_;             // AGENT_MODE_EVENT_ONLY or AGENT_MODE_EVENT_AND_DATA

// Pending events — accumulated between frame sends, cleared after each frame
struct PendingEvent {
    simtime_t firstDetectedAt;
    int       sampleCount;
};
std::unordered_map<std::string, PendingEvent> pendingEntries_; // appeared since last frame
std::unordered_map<std::string, PendingEvent> pendingExits_;   // absent since last frame
```

Renamed `sendUsersInfoSnapshot()` → `sendDataFrame()`. Added `sendEventFrame()`.

R1b also adds here: `inet::TcpSocket controlSocket_;` (config handshake) and the
`controllerControlPort` param wiring. The UDP `controllerSocket_` stays and now
carries only event + data frames.

---

### Piece 7 — `RavensAgentApp.cc` (IN PROGRESS)

**File:** `src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.cc`

#### ✅ Done

**Constructor**: Removed `sendInterval` initialization.

**`initialize()`**: Removed `ttl_`, `forceUpdateInterval_`, `lastSentTimestamp_`,
`hasPendingUpdates_`. Reads `frameInterval_` from NED. Defaults `agentMode_` to
`AGENT_MODE_EVENT_AND_DATA` until ACK received.

**`handleProcessedMessage()` — INFRAESTRUCTURE_DETAILS_ACK**:
- `frameInterval_ = infrastructureDetailsAck->getRate() / 1000.0` (fixes integer division bug)
- `agentMode_ = infrastructureDetailsAck->getAgentMode()`
- Removed `setRetrievalInterval()` call

**`handleLSMessage()`** — fully rewritten and documented:
- Single-pass upsert loop builds `currentLSAddrs` set while processing each UE
- New users: inserted into `users`, added to `pendingEntries_` with `{firstDetectedAt, sampleCount}`
- Reappeared users (were in `pendingExits_`): exit cancelled, added to `pendingEntries_`
- Departure detection after loop: users absent from LS removed from `users`,
  added to `pendingExits_` with accumulating `sampleCount`
- Code 201: kept hardcoded 1s initial delay (harmless — only fires once)
- Added `#include <unordered_set>`

#### Remaining

**`handleRNISMessage()`**:
- Keep cell-level stats block unchanged (PRB usage, PDR)
- Replace per-user loop: aggregate delay and data volume into `AccessPointRadioInfoData`
  instead of writing to `UserData`
- No user map access at all

**`sendEventFrame()`** — new method (sends over UDP `controllerSocket_`):
- If `pendingEntries_` and `pendingExits_` both empty → return, no packet sent
- Otherwise build one `RavensLinkEventMessage` with all events as
  `RavensEvent` structs (ENTRY and EXIT with `sampleCount` + `firstDetectedAt`)
- Send, clear both maps. **Do not reschedule here** — see C4: the timer is
  rescheduled unconditionally in `handleSelfMessage()`, not on the send path.

**`sendDataFrame()`** — replaces `sendUsersInfoSnapshot()` (UDP `controllerSocket_`):
- Skip if `agentMode_ != AGENT_MODE_EVENT_AND_DATA`
- Compute `avg_distance_to_ap` from `users` map, call setter on `accessPointRadioInformation`
- Build and send `RavensLinkDataFrameMessage`
- Called from same timer handler as `sendEventFrame()`

**`handleSelfMessage()`** — update timer dispatch:
- `sendUserList` message → **first** reschedule at `frameInterval_` (always; C4),
  **then** call `sendEventFrame()` then `sendDataFrame()`
- Remove `sendUsersInfoSnapshot()` call

---

### Piece 8 — `RavensControllerApp.ned`

**File:** `src/apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.ned`

Remove:
```ned
int bufferTime = default(3);
```

Add:
```ned
int confirmationCount = default(2);       // min samplesSinceChange on an ENTRY from a
                                          // new MEH before handover is accepted (see C2)
double frameInterval = default(3);        // frame interval sent to Agents via ACK (s)
int exitConfidenceThreshold = default(2); // min samplesSinceChange on EXIT to act on it
int controlPort = default(5000);          // R1c: TCP listen port for the config handshake
```

Note: `exitTTL` (a timer) is NOT added. The Agent's `samplesSinceChange` field
on EXIT frames replaces it — the Controller reads confidence from the data,
not from a local stopwatch. (`localPort = 5001` stays as the UDP data-plane port.)

---

### Piece 9 — `RavensControllerApp.h`

**File:** `src/apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.h`

Update `UserState`:
```cpp
struct UserState {
    std::string userId;
    std::string currentMEH;
    simtime_t timestamp;
    UserData userData;
    std::string pendingMEH;           // MEH attempting handover (empty if none)
    // NOTE (C2): the cross-frame confirmation counter does not work with
    // report-entry-once. Confirm on samplesSinceChange ≥ confirmationCount_ from
    // a single ENTRY instead. Drop pendingConfirmations unless C2 is resolved
    // otherwise.
    // REMOVED: simtime_t lastHandoverTime
};
```

Remove method declaration:
```cpp
bool shouldAcceptHandover(const std::string& userId, const std::string& newMEH);
```

Add members:
```cpp
int confirmationCount_;
int exitConfidenceThreshold_;
double frameInterval_;
```

Add method declaration:
```cpp
void handleEventFrame(inet::Ptr<const RavensLinkEventMessage> event,
                      inet::L3Address remoteAddress, int srcPort);
```

R1c also adds here: a listening `inet::TcpSocket serverSocket;`, the
`inet::TcpSocket::ICallback` overrides, and use of the already-declared
`inet::SocketMap socketMap;` for accepted control-plane connections.

---

### Piece 10 — `RavensControllerApp.cc`

**File:** `src/apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.cc`

**`initialize()`**:
- Read `confirmationCount_`, `exitConfidenceThreshold_`, `frameInterval_` from NED

**Control plane → TCP (R1c):** the `JOIN_NETWORK_REQUEST` and
`INFRAESTRUCTURE_DETAILS` branches move out of the UDP `socketDataArrived` into
the TCP `socketDataArrived(TcpSocket*, ...)` path; `sendJoinNetworkAck` /
`sendInfrastructureDetailsAck` write back on the accepting `TcpSocket` instead of
`udpSocket->sendTo(...)`. See R1c for the listen/accept setup.

**`sendInfrastructureDetailsAck()`**:
- `setRate((int)(frameInterval_ * 1000))` — send interval in ms (replaces the
  hardcoded `setRate(3000)`)
- `setAgentMode(...)` based on configured handler policy

**`socketDataArrived(UdpSocket*)`** (data plane only after R1c):
- Add branch for `UE_EVENT` → call `handleEventFrame()`
- Rename `USERS_INFO_SNAPSHOT` references to `DATA_FRAME`

**`handleEventFrame()`** — new method:

```
On EVENT_ENTRY (samplesSinceChange = N):
  - If user not in map → new entry (signal handler policy)
  - If user in map, same MEH → refresh timestamp
  - If user in map, different MEH (handover):
      - if N >= confirmationCount_ → HANDOVER CONFIRMED (signal policy, set currentMEH = srcMEH)
      - else → low confidence; record pendingMEH = srcMEH and wait for the
        exit-hold / next signal (see C2/F2 — confirm on the single event's
        confidence, NOT by accumulating across frames)

On EVENT_EXIT (samplesSinceChange = N):
  - Ignore if srcMEH != currentMEH (stale exit from the cell the user already left — C5)
  - If N < exitConfidenceThreshold_: low confidence, log but do not act
  - If N >= exitConfidenceThreshold_: hold briefly (≈1 frameInterval, F2); if an
    ENTRY from a different MEH arrives within the window → reclassify as handover,
    else → emit departure
```

> The confirmation/exit-hold details above encode the still-open decisions
> **C2, C5, F2**. Settle them before implementing this method (see the status
> snapshot). `removeInactiveUsers()` remains only as a lost-UDP-packet safety net
> with `threshold_ ≫ frameInterval` (C1/M1).

**`updateUserStateMap()`**:
- Now only updates `userData` content on DATA_FRAME (location, RNIS aggregates)
- Handover/departure decisions are driven by `handleEventFrame()`, not this method
- Remove `HANDOVER_LOCKOUT`, `MIN_UPDATE_INTERVAL` hardcoded constants
- Remove `shouldAcceptHandover()` call

**Remove `shouldAcceptHandover()`** entirely.

---

### Piece 11 — Handler policies

**`NotifyOnDataChange.cc/.h`**:
- Remove `shouldAcceptHandover()` call
- Remove `getDlNongbrDelayUe() == -1` filter
- Remove unused members: `interval_`, `start`, `stanby_treshold_`,
  `max_iterations`, `standby`, `ueStanbyElement`
- Entry/exit/handover now arrive via `handleEventFrame()` path

**`SendToExternalServer.cc`**:
- Remove `shouldAcceptHandover()` call
- Remove `getDlNongbrDelayUe() == -1` filter
- Update `formatSnapshot()`: use cell-level aggregates from `AccessPointRadioInfoData`
  instead of per-user RNIS fields
  ```json
  {
    "mecHostId": "...",
    "timestamp": "...",
    "cellMetrics": {
      "dl_total_prb_usage", "ul_total_prb_usage",
      "dl_nongbr_pdr_cell", "ul_nongbr_pdr_cell",
      "number_of_active_ue_dl_nongbr_cell",
      "avg_dl_delay", "avg_ul_delay",
      "total_dl_data_volume", "total_ul_data_volume",
      "avg_distance_to_ap"
    },
    "users": [
      { "ueId", "address", "accessPointId",
        "x", "y", "z", "speed", "bearing", "distanceToAp" }
    ]
  }
  ```

**`SaveDataHistory.cc`**:
- Remove per-user RNIS columns from user CSV
- Add cell-level aggregate columns to radio stats CSV
- Remove `getDlNongbrDelayUe() == -1` filter

---

### Piece 12 — Dead code cleanup

- Remove `sendUserListRequest()`, `sendUserLocationRequest()`,
  `sendUsersDensitySubscription()` from Agent if unused
- Renumber `USERS_UPDATE` / `MIGRATION_PLAN` to a non-overlapping range (20/21)
  to clear the `7` collision with `UE_EVENT` (folded into R1a / F3)
- Clean up any remaining `USERS_INFO_SNAPSHOT` and "control frame" string
  references in EV logs (→ `DATA_FRAME` / "event frame")

---

### Piece 13 — Event→MEO update wiring (semantic policy hooks)

> Added 2026-06-29. Closes the 🔴 gap flagged in the status snapshot: confirmed
> ENTRY/HANDOVER/EXIT events currently produce **no** `UserMEHUpdate` for the MEO
> (only `SaveDataHistory` overrides `handleEventMessage`, and only to log). This
> Piece is **settled and ready for Sonnet to implement** — design decisions below
> are final, do not re-litigate.
>
> **Settled product decisions (2026-06-29):**
> - **SaveDataHistory logs only — it does NOT notify the MEO** (no `UserMEHUpdate`,
>   not even for safety-net exits).
> - **Lifecycle CSV uses *resolved transitions*** — exactly one row per final
>   outcome, emitted at the F2 decision point (an A→B handover that arrives as
>   EXIT-from-A then ENTRY-at-B produces a single `HANDOVER` row, never a separate
>   `EXIT` row).

#### Design: Controller classifies, policy decides

`handleEventFrame` already holds the authoritative ENTRY/HANDOVER/EXIT
classification (it owns `pendingExitTime`, `currentMEH`, the C5 guard, the F2
hold). It must **not** be re-derived inside the policies. Instead the Controller
calls three semantic hooks on the policy at each authoritative decision point, and
each policy decides whether/how to act:

- `NotifyOnDataChange` (reactive) → emit a `UserMEHUpdate` on every hook.
- `SendToExternalServer` (reactive+proactive) → same as reactive for the event
  path; ML migration predictions continue via the existing Flask path.
- `SaveDataHistory` → write one lifecycle CSV row per hook; **no** `UserMEHUpdate`.

The hooks live on the policy (not emitted unconditionally by the Controller)
*specifically* so a future proactive-only config (C3) can suppress event-driven
updates and exercise the ML path in isolation.

#### 13.1 — `LocationDataHandlerPolicyBase.h` (+ new `.cc`)

Remove the old single notifier:
```cpp
virtual void handleEventMessage(const RavensEventList& events, const std::string& sourceMEH) {}  // DELETE
```

Add three virtual hooks (default empty) — primitives only, so `RavensEvent` does
not leak into the policy interface:
```cpp
virtual void onUserEntry   (const std::string& userId, const std::string& meh,
                            int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}
virtual void onUserHandover(const std::string& userId, const std::string& fromMeh,
                            const std::string& toMeh,
                            int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}
virtual void onUserExit    (const std::string& userId, const std::string& fromMeh,
                            int samplesSinceChange, omnetpp::simtime_t firstDetectedAt) {}
```

Hoist the duplicated `addUserUpdate` (currently identical in all 3 policies, used
only for safety-net exits) into a single protected non-virtual helper. Create
`LocationDataHandlerPolicyBase.cc` for it (header-only today):
```cpp
protected:
    void emitUserUpdate(const std::string& address,
                        const std::string& lastMeh,
                        const std::string& newMeh);  // insert_or_assign into controllerApp_->userUpdates
```
Add the new `.cc` to the build (it is picked up automatically by the Simu5G
`opp_makemake` glob — no makefile edit, but confirm it compiles).

#### 13.2 — `RavensControllerApp.h` — `UserState` exit-event stash

The EXIT `UserMEHUpdate`/CSV row is emitted at hold-expiry (Step 1), where the
original EXIT event is gone. Stash its confidence fields when the hold starts:
```cpp
struct UserState {
    ...
    simtime_t pendingExitTime;        // already present (F2)
    int       pendingExitSamples = 0; // NEW: samplesSinceChange of the EXIT that opened the hold
    simtime_t pendingExitFirstAt;     // NEW: firstDetectedAt of that EXIT
};
```

#### 13.3 — `RavensControllerApp.cc` — call hooks at decision points

Delete the Step 3 pre-update `handleEventMessage(allConfirmed, sourceMEH)` call
(and the `allConfirmed` assembly that feeds it). Replace with hook calls at the
authoritative points:

- **Step 1 (hold expired → departure):** before `userStateMap.erase(it)`, call
  `locationDataHandlerPolicy_->onUserExit(userId, currentMEH, pendingExitSamples, pendingExitFirstAt)`.
- **Step 4 (start hold):** in addition to `pendingExitTime`, stash
  `pendingExitSamples = e.samplesSinceChange; pendingExitFirstAt = e.firstDetectedAt;`.
  **No hook here** — not resolved yet. (C5 guard unchanged: only the current MEH
  may open a hold.)
- **Step 5 (entries)** — refined branch logic:
  ```
  user NOT in map:
      add (currentMEH = sourceMEH, pendingExitTime = 0); onUserEntry(...)
  user in map, pendingExitTime != 0 (was in hold):
      cancel hold (pendingExitTime = 0)
      if sourceMEH != currentMEH:  onUserHandover(currentMEH→sourceMEH); currentMEH = sourceMEH
      else:                        re-entry/flap at same MEH — no hook, just refresh
  user in map, no hold:
      if sourceMEH != currentMEH:  onUserHandover(currentMEH→sourceMEH); currentMEH = sourceMEH   // ENTRY-before-EXIT / C5 order
      else:                        duplicate ENTRY — no hook, just refresh
  (always refresh timestamp)
  ```
  This guarantees exactly one HANDOVER hook per transition in **both** arrival
  orders (EXIT-first via the hold-cancel branch; ENTRY-first via the direct
  branch), and never a spurious A→A handover on a flap.

#### 13.4 — Policy implementations

**`NotifyOnDataChange` (.h/.cc):** override all three hooks → `emitUserUpdate`:
- `onUserEntry`    → `emitUserUpdate(userId, "",      meh)`
- `onUserHandover` → `emitUserUpdate(userId, fromMeh, toMeh)`
- `onUserExit`     → `emitUserUpdate(userId, fromMeh, "")`

Replace the safety-net loop in `handleDataMessage` (`removeInactiveUsers()`) to
call `this->onUserExit(user.userId, user.currentMEH, -1, SIMTIME_ZERO)` instead of
building a `UserMEHUpdate` inline (`-1` samples = "inactivity purge, not a counted
EXIT"). Delete the per-policy `addUserUpdate` (now `emitUserUpdate` in base).

**`SendToExternalServer` (.h/.cc):** identical hook overrides to
`NotifyOnDataChange` (→ `emitUserUpdate`). Same safety-net refactor. The Flask /
`migrationPredictions` path in `handleDataMessage` is unchanged. Delete its
`addUserUpdate`.

**`SaveDataHistory` (.h/.cc):** replace `handleEventMessage` with the three hooks,
each writing one lifecycle CSV row (`timestamp,eventType,userId,fromMEH,toMEH,
samplesSinceChange,firstDetectedAt`) and flushing:
- `onUserEntry`    → row `ENTRY,   userId, "",      meh,    samples, firstAt`
- `onUserHandover` → row `HANDOVER,userId, fromMeh, toMeh,  samples, firstAt`
- `onUserExit`     → row `EXIT,    userId, fromMeh, "",     samples, firstAt`

**No `emitUserUpdate` calls in SaveDataHistory** (log-only decision). Its
safety-net loop in `handleDataMessage` must **stop** calling `addUserUpdate` — it
should call `this->onUserExit(...)` so the purge is logged to CSV but **not** sent
to the MEO. (This also fixes a current inconsistency where SaveDataHistory pushes
safety-net exits into `userUpdates`.)

#### 13.5 — Correctness notes / invariants

- All events in one `handleEventFrame` call share one `sourceMEH` (one UDP frame
  from one Agent), so ENTRY and EXIT for the *same* user never co-occur in a call.
- The per-address `userUpdates` map (`insert_or_assign`) already collapses multiple
  transitions for one UE within a `snapshot_frequency_` window to the latest —
  acceptable (MEO acts on net effect).
- `firstDetectedAt` is now consumed (lifecycle CSV) → resolves critique **M2**.
- Hooks are called *during* map mutation but take explicit args, so they never
  depend on whether the map has been updated yet (removes the fragility of the old
  pre-update `handleEventMessage`).

#### 13.6 — Verification (add to the main list)

14. **Reactive MEO update** — a confirmed ENTRY at a new MEH yields a
    `UserMEHUpdate{last:"", new:MEH}` in the next MEO snapshot.
15. **Handover MEO update, both orders** — A→B handover yields exactly one
    `UserMEHUpdate{last:A, new:B}` whether EXIT-from-A or ENTRY-at-B arrives first,
    and exactly one `HANDOVER` lifecycle row (no stray `EXIT` row).
16. **Departure at hold-expiry** — a true EXIT (no follow-up ENTRY) yields
    `UserMEHUpdate{last:MEH, new:""}` one frame later, plus one `EXIT` row.
17. **SaveDataHistory is MEO-silent** — running `SaveDataHistory`, `userUpdates`
    is never populated from events or from the safety-net purge.
18. **No A→A handover on flap** — EXIT then quick re-ENTRY at the same MEH emits no
    handover hook.

---

## Design Revision D1 — Remove the confidence gate (presence is unconditional)

> Added 2026-06-29 (second pass), after empirically hitting the failure during
> testing. **Implemented.** Supersedes the C2 "threshold the single event"
> resolution. Decision is settled.

### Why

The Controller filtered ENTRY on `samplesSinceChange ≥ confirmationCount_` and EXIT
on `≥ exitConfidenceThreshold_`. Two problems, one fatal:

1. **There is nothing to filter.** LS is the single source of truth for presence
   (Development Consideration #1), and the radio layer guarantees a UE is attached
   to exactly one cell. Every LS appearance is a *real* attachment, not noise. A
   "confidence gate" on presence is filtering a signal that has no false positives.
2. **Report-once makes a dropped event permanent.** The Agent reports each ENTRY
   exactly once, then omits the (now stable) user from all future frames — that is
   the whole bandwidth win. So an ENTRY whose first 1s sample lands in the last
   sub-frame window before a frame fires carries `samplesSinceChange = 1` purely by
   frame phase (≈1/3 of entries with `frameInterval=3`). With the gate at 2 it was
   dropped and **never re-sent** → the user was invisible to the Controller forever
   (the inactivity safety net can't help: it only removes users already in the map).

`samplesSinceChange` conflates "low-confidence flicker" with "legitimate but late in
the window" — a single threshold cannot separate them, and guessing wrong = data loss.

> Note: this only became visible after fixing bug **B1** (the Agent counter was
> stuck at 1 for *every* event, so the gate had been dropping everything; DATA_FRAME
> seeding the map masked it for EXIT-only). B1 fix + D1 together are what make the
> lifecycle correct.

### What changed

- **`handleEventFrame` Step 2** no longer gates — it splits events by type and acts
  on all of them. A cold ENTRY (unknown user) always creates the user + fires
  `onUserEntry`. Boundary-phase entries are no longer lost.
- **The F2 hold is the sole debounce.** Handover-vs-departure is decided purely by
  the time-based hold (which is also the only mechanism that can reason across the
  two independent Agents). A single-sample EXIT opens a hold immediately; reappear
  at same MEH → flap (no-op), elsewhere → handover, nowhere → departure.
- **`samplesSinceChange` / `firstDetectedAt` are metadata** — logged in the
  lifecycle CSV (detection confidence, detection→action latency), not gates.
- **Deleted:** `confirmationCount_` / `exitConfidenceThreshold_` (members, `par()`
  reads, NED params, ini lines). One Controller timing knob remains: the hold =
  `frameInterval`.

### Rejected alternative

Keep confidence-gating *correctly* by breaking report-once (Agent re-sends an
unconfirmed entry each frame until it clears the threshold or the user leaves).
Rejected: it adds Agent state, multiplies entry latency by several frames, and
re-spends the bandwidth the delta model was built to save — all to "confirm"
something LS already reported authoritatively.

---

## Revision R1 — Terminology fix + TCP control plane

> Added 2026-06-29 after a design review. Two orthogonal changes that the rest of
> the plan (Pieces 1–12) predates. **Apply R1 first**, then read every earlier
> Piece with the rename map below in effect. Decisions are settled — do not
> re-litigate; just implement. Implementation target: Sonnet.

### R1 background — why

The plan called the ENTRY/EXIT delta frame a "**control frame**." That name is
wrong on the control-plane / data-plane axis: ENTRY/EXIT events are *telemetry*
(the Agent reporting what it observed) — they are **data plane**, exactly like
the "data frame." The only genuine **control plane** in the system is the
registration/config handshake (`JOIN_NETWORK_*`, `INFRAESTRUCTURE_DETAILS_*`,
and the ACK that sets `agentMode` + `frameInterval`). R1 makes the vocabulary
match reality and moves the config handshake onto a reliable transport.

Three lanes, two planes:

| Lane | Messages | Transport | Plane |
|---|---|---|---|
| **Control / signaling** | JOIN, INFRAESTRUCTURE_DETAILS, both ACKs | **TCP** | control |
| **Event** (was "control frame") | `UE_EVENT` / `RavensLinkEventMessage` | UDP | data |
| **Data** | `DATA_FRAME` / `RavensLinkDataFrameMessage` | UDP | data |

Settled decisions:
1. **Rename** the event-delta lane: "control frame" → "**event frame**".
2. **TCP for the config handshake only.** Event + Data frames stay UDP (each Data
   frame supersedes the last; the bandwidth comparison measures the UDP feed).
3. **Close-after-handshake** TCP connection. No dynamic mid-run reconfig is
   needed — the three experimental configs are separate runs with config fixed at
   registration. (If push-reconfig is ever wanted, keep the Agent-initiated
   connection *open* and have the Controller write down it — do **not** make the
   Agent a TCP server. That asymmetry is why persistent beats new-connection.)

### R1a — Rename map (event-frame terminology)

Pure mechanical rename, applied everywhere (defines, `.msg`, `.h`, `.cc`, prose,
EV logs). Apply before/with the earlier Pieces so they're written in the new
vocabulary.

| Old | New |
|---|---|
| `UE_CONTROL_EVENT` (define) | `UE_EVENT` |
| `CONTROL_ENTRY` / `CONTROL_EXIT` | `EVENT_ENTRY` / `EVENT_EXIT` |
| `AGENT_MODE_CONTROL_ONLY` | `AGENT_MODE_EVENT_ONLY` |
| `AGENT_MODE_CONTROL_AND_DATA` | `AGENT_MODE_EVENT_AND_DATA` |
| `RavensLinkControlEventMessage` | `RavensLinkEventMessage` |
| `RavensControlEvent` / `RavensControlEventList` | `RavensEvent` / `RavensEventList` |
| `sendControlEvents()` (Agent) | `sendEventFrame()` |
| `handleControlEvent()` (Controller) | `handleEventFrame()` |
| prose "control frame" / "control event" | "event frame" / "event" |

Field names inside the struct (`ueAddress`, `eventType`, `samplesSinceChange`,
`firstDetectedAt`) are unchanged. The `agentMode` enum is renamed, not
restructured — the C3 critique (two booleans for the "proactive-only" config) is
a **separate** open decision, not part of R1.

**Fold in F3 while renaming.** `UE_CONTROL_EVENT = 7` collides by value with the
MEO-facing `#define USERS_UPDATE 7` (`RavensControllerApp.cc:12`). Give `UE_EVENT`
a fresh value and renumber the MEO codes to a non-overlapping range, e.g.:
- Agent↔Controller data plane: `DATA_FRAME = 6`, `UE_EVENT = 7`
- Controller→MEO: `USERS_UPDATE = 20`, `MIGRATION_PLAN = 21`

Files touched by R1a: both app `.h` defines (`RavensAgentApp.h:9–22`,
`RavensControllerApp.h:5–18`), `RavensLinkPacket.msg`, `RavensAgentApp.{h,cc}`,
`RavensControllerApp.{h,cc}`, all three handler policies, and the prose in this
plan (Pieces 1, 2, 6, 7, 9, 10, 11; Architecture §"Two frame types").

### R1b — TCP control plane (Agent side)

**Files:** `RavensAgentApp.ned`, `RavensAgentApp.h`, `RavensAgentApp.cc`

Current state (grounded): the Agent already runs TCP sockets to the MEC platform
services (`mp1Socket_`, `lsSocket_`, `rnisSocket_`) with the full lifecycle —
`addNewSocket()`, `connect()`, `socketEstablished()`, `socketClosed(TcpSocket*)`
— and one **UDP** socket `controllerSocket_` to the Controller
(`RavensAgentApp.cc:455–464`). The handshake (JOIN/DETAILS) and the frames all go
over that UDP socket today (`handleProcessedMessage` at `:761`, sends at
`:135/154/219`).

Changes:
1. **NED:** add `int controllerControlPort = default(5000);` (TCP). Keep
   `controllerPort = default(5001)` (UDP, now data plane only).
2. **Add a TCP control socket.** New member `inet::TcpSocket controlSocket_;`.
   `controllerSocket_` (UDP) stays and now carries **only** `UE_EVENT` +
   `DATA_FRAME`. (Optionally rename it `dataSocket_` for clarity — low-churn,
   your call.)
3. **Connection bring-up** (`connectToRavensController`, `:455`): set up *both*
   sockets — bind the UDP socket as today, and `setOutputGate`/`setCallback`/
   `connect()` the TCP `controlSocket_` to `controllerControlPort`. The single
   `socketOut` gate carries both (same dispatcher that already serves the UDP
   socket + 3 TCP service sockets — no new NED gates).
4. **Use object/message transfer mode** on `controlSocket_` so RavensLink message
   boundaries are preserved across the TCP stream (avoids manual byte-stream
   reassembly). This matches how the MEC-service TCP sockets deliver whole
   messages. *Sonnet: confirm the INET `TcpSocket` transfer-mode setting used by
   the service sockets and mirror it.*
5. **Handshake over TCP:** send `JOIN_NETWORK_REQUEST` from the
   `controlSocket_` branch of `socketEstablished()` (the method already switches
   on `connId` at `:81–115` — add a `controlSocket_` case). Route incoming
   `JOIN_NETWORK_ACK` / `INFRAESTRUCTURE_DETAILS_ACK` through the TCP path; the
   existing logic in `handleProcessedMessage` (`:780–790`) moves from the
   `controllerSocket_.belongsToSocket(msg)` (UDP) branch to a
   `controlSocket_.belongsToSocket(msg)` (TCP) branch. Behaviour is identical;
   only the socket changes.
6. **Close after handshake:** in the `INFRAESTRUCTURE_DETAILS_ACK` handler, after
   reading `frameInterval_`/`agentMode_` and scheduling the LS/RNIS subscription,
   `controlSocket_.close()`. The data plane (UDP) is independent and already
   identifies the Agent by `mecHostId` carried in every frame payload — the TCP
   connection is not needed post-handshake.

### R1c — TCP control plane (Controller side)

**Files:** `RavensControllerApp.ned`, `RavensControllerApp.h`, `RavensControllerApp.cc`

Current state (grounded): `ApplicationBase` + `UdpSocket::ICallback`, single
`udpSocket` bound to `localPort` (`:95–98`), and an **already-declared but unused**
`inet::SocketMap socketMap;` (`RavensControllerApp.h:82`) — the exact INET idiom
for managing accepted TCP connections. Handshake handled in
`socketDataArrived(UdpSocket*)` (`:181–225`); ACKs sent via `socket->sendTo(...)`
(`:237–261`).

Changes:
1. **NED:** add `int controlPort = default(5000);` (TCP listen). Keep
   `localPort = default(5001)` (UDP, data plane).
2. **Become a TCP server.** Add a listening `inet::TcpSocket serverSocket;`
   (`setOutputGate`, `bind(controlPort)`, `listen()`), and implement
   `inet::TcpSocket::ICallback`. On `socketAvailable` → accept into a new
   `TcpSocket`, set its callback, store in the existing `socketMap`. *Verify the
   Controller's containing node has a `Tcp` module* — it is modeled on UALCMP
   (TCP server), so it almost certainly does; Sonnet must confirm in the network
   `.ned`/`.ini` before relying on it.
3. **Move the handshake to TCP.** Relocate the `JOIN_NETWORK_REQUEST` and
   `INFRAESTRUCTURE_DETAILS` branches (`:181–225`) out of the UDP
   `socketDataArrived` and into the TCP `socketDataArrived(TcpSocket*, Packet*)`
   path. `sendJoinNetworkAck` / `sendInfrastructureDetailsAck` change signature
   from `(UdpSocket*, L3Address, port)` to writing back on the accepted
   `TcpSocket` (`tcpSocket->send(packet)`).
4. **The UDP `socketDataArrived` now handles only data plane:** `UE_EVENT`
   (→ `handleEventFrame()`, new) and `DATA_FRAME` (→ handler policy). This is the
   same branch work Piece 10 already specifies — just note the handshake no
   longer arrives here.
5. **Agent identity:** `MECHostData` is still keyed by `mecHostId` from the JOIN
   payload; record the accepting connId if useful, but since the Agent closes
   after handshake and all frames carry `mecHostId`, the Controller does not need
   the TCP connection post-handshake. Handle `socketClosed`/`peerClosed` by
   removing the connection from `socketMap` (the host registration in
   `mehStateMap` persists).
6. **Fix `sendInfrastructureDetailsAck` while here:** it currently hardcodes
   `setRate(3000)` and never calls `setAgentMode(...)` (`:249–261`). Set
   `setRate((int)(frameInterval_ * 1000))` and `setAgentMode(...)` per the
   configured handler policy (this is the Piece 10 work, now on the TCP path).

### R1 — what this does and does NOT buy

- **Does:** the config handshake can no longer be silently lost — a dropped ACK
  over UDP previously risked leaving an Agent in its default `agentMode` /
  `frameInterval`. Now reliable + ordered.
- **Does NOT:** fix the EXIT-before-ENTRY cross-Agent race (critique **F2/C5**).
  TCP orders bytes *within one connection*; EXIT and ENTRY come from *two
  different Agents* over UDP. The short Controller-side exit-hold window (F2) is
  still required. Do not let R1 be mistaken for solving it.
- **Methodology note for the thesis:** the handshake is one-time and tiny, so TCP
  overhead does not affect the per-frame bandwidth comparison (which measures the
  UDP event/data feed). No change to the measurement story.

### R1 — added verification items

Folded into the main Verification list below as items 11–13.

---

## Before vs. After

| Aspect | RAVENS | eRAVENS |
|---|---|---|
| **Event frame timing** | No event frame — one snapshot type | Periodic at `frameInterval` (1–5s, configurable) |
| **Departure detection** | TTL purge at snapshot time (up to 10s late) | LS diff at 1s, reported at next frame |
| **Confidence signal** | None — Controller uses time-based lockout | `samplesSinceChange` per event — Agent already did the counting |
| **Handover logic** | `HANDOVER_LOCKOUT = 10s` hardcoded timer | F2 exit-hold (≈1 `frameInterval`); no confidence gate (D1) |
| **exitTTL** | Implicit in HANDOVER_LOCKOUT | Replaced by `exitConfidenceThreshold_` + short exit-hold window (F2) |
| **Per-user RNIS** | 6 fields per user in every snapshot | None — dropped entirely |
| **Cell-level metrics** | 4 fields (PRB, PDR) | 4 existing + 5 new aggregates |
| **MEH compute** | Minimal (LS polling only) | LS diff, sample counting, RNIS aggregation |
| **Rate negotiation** | Broken (hardcoded + integer division) | Fixed: NED param on Controller, correct float conversion |
| **Agent mode** | Single mode | EVENT_ONLY or EVENT_AND_DATA |
| **Transport** | All traffic over UDP | Control plane (handshake) TCP; data plane (event + data frames) UDP |
| **Hidden constraints** | `ttl_ + forceUpdate < HANDOVER_LOCKOUT` | One localized exit-hold window (F2); `threshold_ ≫ frameInterval` (M1) |

---

## Verification

After all pieces are implemented:

1. **Compile** — project builds cleanly
2. **Event frame content** — EV log shows `samplesSinceChange` increments correctly across LS cycles
3. **Departure detection** — Agent removes departed user from map within 1s, reports EXIT in next event frame with correct `samplesSinceChange`
4. **No stale users** — Agent never sends a departed UE in a data frame
5. **Mode enforcement** — EVENT_ONLY Agent sends no data frames
6. **Rate negotiation** — Agent adopts Controller's `frameInterval` correctly (no integer division)
7. **Handover (D1)** — an ENTRY at a new MEH for a known user produces exactly one handover (no threshold); both EXIT-first and ENTRY-first arrival orders converge to one `onUserHandover`
8. **Departure via hold (D1/F2)** — a single-sample EXIT opens an F2 hold; with no re-entry it becomes a departure ~1 `frameInterval` later (driven by the `expireHolds` timer even with no further frames)
9. **Cell aggregates** — `AccessPointRadioInfoData` carries correct avg/total values
10. **Flask format** — `SendToExternalServer` JSON matches new schema
11. **TCP handshake (R1)** — handshake completes over TCP; Agent closes `controlSocket_` after `INFRAESTRUCTURE_DETAILS_ACK`; Controller drops the connection from `socketMap` on close without losing the `mehStateMap` registration
12. **Transport split (R1)** — Event + Data frames still flow over UDP after the TCP control connection closes
13. **No define collision (R1/F3)** — `UE_EVENT` value is unique vs the MEO codes (`USERS_UPDATE`/`MIGRATION_PLAN` → 20/21)

---

## File change summary

| File | Status |
|---|---|
| `RavensAgentApp.h` | ✅ Piece 1 done |
| `RavensControllerApp.h` | ✅ Piece 1 done |
| `RavensLinkPacket.msg` | ✅ Piece 2 done |
| `UserData.h/.cc` | ✅ Piece 3 done |
| `AccessPointRadioInfoData.h/.cc` | ✅ Piece 4 done |
| `RavensAgentApp.ned` | ✅ Piece 5 done |
| `RavensAgentApp.h` | ✅ Piece 6 done |
| `RavensAgentApp.cc` | ✅ Piece 7 done; + sampleCount accumulation fixes (ENTRY & EXIT); `numberOfActiveUeDlNongbrCell` populated |
| `RavensControllerApp.ned` | ✅ Piece 8 done (ports: `dataPort`/`mgmtPort`); confidence params removed (D1) |
| `RavensControllerApp.h` | ✅ Piece 9 done (+ `pendingExitTime`, `pendingExitSamples/FirstAt`, `expireHoldsMsg_`) |
| `RavensControllerApp.cc` | ✅ Piece 10 + 13 + D1: `handleEventFrame` no-gate, C5 guard, timer-driven `expirePendingExits()`, `updateUserStateMap` no longer seeds map |
| `LocationDataHandlerPolicyBase.h/.cc` | ✅ Piece 13: 3 semantic hooks + `emitUserUpdate` helper (new `.cc`) |
| `NotifyOnDataChange.cc/.h` | ✅ Piece 13: hooks → `emitUserUpdate`; safety net → `onUserExit`; dead standby state removed |
| `SendToExternalServer.cc` | ✅ Piece 13 + 11: hooks → `emitUserUpdate`; `formatSnapshot` cell-aggregate JSON schema |
| `SaveDataHistory.cc` | ✅ Piece 13 + 11: 3 hooks → lifecycle CSV (log-only); user CSV + radio_stats full cell-metric columns |
| Dead code | Piece 12 — not started |
| **R1a — event-frame rename** | ✅ done |
| **R1b — Agent TCP mgmt socket** | ✅ `RavensAgentApp.{ned,h,cc}` (`controllerMgmtSocket_`) |
| **R1c — Controller TCP server** | ✅ `RavensControllerApp.{ned,h,cc}` (`serverSocket_`) |

---

## Plan Review (pre-implementation critique)

> Added as a review pass. Nothing below is implemented — these are issues found
> while re-reading the plan against the current Controller code. Ordered by
> severity. Each item has a proposed alternative.
>
> **Terminology note (R1):** this section predates R1 and still uses the old
> "control frame" / `CONTROL_*` names. Read them as "event frame" / `EVENT_*` /
> `UE_EVENT` / `handleEventFrame()` per the R1a rename map. The *substance* of
> C1–C5 (departure authority, confirmation model, proactive-only config, frame
> timer, stale-exit race) is unchanged by R1 and **remains open** — R1 only
> renamed things and moved the handshake to TCP.

### 🔴 Critical — these break a stated goal if left as-is

#### C1. Stable users get purged in CONTROL_ONLY mode (no keepalive)

**Problem.** `removeInactiveUsers()` (Controller `.cc:275`) purges any user whose
`UserState.timestamp` is older than `threshold_`. In the current system that
timestamp is refreshed on **every snapshot**, because each snapshot carries the
full user map. In eRAVENS:
- Control frames carry **only changes** (line 62: "stable users are omitted").
- In `AGENT_MODE_CONTROL_ONLY` there are **no data frames at all**.

So a user who enters MEH-A and stays put generates exactly **one** ENTRY event,
then silence. Their Controller-side `timestamp` never refreshes → after
`threshold_` seconds `removeInactiveUsers()` deletes a user who never left.
This is the old "stale users" bug inverted into a "vanishing users" bug, and it
only manifests in the reactive-only experimental config — i.e. the cheapest,
most important one for the bandwidth comparison.

**Proposed alternative.** Make CONTROL_EXIT the *authoritative and only* source
of departure. Because the Agent's LS diff is guaranteed to emit an EXIT when a
user disappears (Piece 7 departure loop), the Controller does not need
inactivity-based purging as its primary mechanism.
- Primary: remove a user only on a confirmed CONTROL_EXIT.
- Safety net: keep `removeInactiveUsers()` but with `threshold_ >> frameInterval`
  (e.g. tens of frames), documented explicitly as a lost-packet fallback (UDP),
  **not** the normal path. State this contract in Piece 10 so it is not silently
  re-tightened later.

#### C2. Cross-frame confirmation counter is incompatible with "report entry once" — ✅ SUPERSEDED by D1

> First resolved (Piece 10) by thresholding the single ENTRY's `samplesSinceChange`
> against `confirmationCount_`. **That resolution was itself flawed** and is now
> withdrawn — see **D1** (Design Revisions). The threshold > 1 still permanently
> dropped any entry whose first sample landed in the last sub-frame window (~1/3 of
> entries), because report-once gives no second chance. **D1 removes the confidence
> gate entirely:** presence is unconditional (LS is truth), and the F2 hold is the
> sole debounce. `samplesSinceChange`/`firstDetectedAt` are now logged metadata.
> `confirmationCount_` / `exitConfidenceThreshold_` deleted.

**Problem.** The Agent reports a user's ENTRY exactly once: after the entry is
flushed in a control frame, `pendingEntries_` is cleared and the (now stable)
user is omitted from all future frames. But the Controller logic (Piece 10,
lines 420–424) and the architecture sketch (lines 114–116) wait for **N
consecutive ENTRY frames** from the new MEH:

```
pendingConfirmations += N ... on next control frame from MEH-B → CONFIRMED
```

There is no "next control frame with the same user" — it is sent once. If
`confirmationCount_ > samplesSinceChange` of that single event, the handover is
**never** confirmed. The plan mixes two confirmation models (accumulate-across-
frames *and* compare-to-threshold) that cannot coexist with delta reporting.

**Proposed alternative.** Confirm on the confidence already inside the single
event: **handover confirmed when `samplesSinceChange >= confirmationCount_`.**
Drop the cross-frame accumulation entirely. The Agent has already done the
counting (the whole point of `samplesSinceChange`); the Controller just
thresholds it. Update Piece 10's `handleControlEvent` and Verification item #7
accordingly (#7 currently restates the broken model).

#### C3. "Proactive only" experimental config is not expressible

**Problem.** The research framing (lines 131–146) needs three configs:
reactive-only, **proactive-only (data frames, no control)**, and both. But:
- Control frames are "always sent regardless of agent mode" (line 56).
- `agentMode` has only `CONTROL_ONLY` and `CONTROL_AND_DATA`.

There is no way to send data frames **without** control frames, so config #2
cannot be run. This silently undercuts the primary comparison the redesign is
built to enable.

**Proposed alternative.** Replace the 2-value `agentMode` with two independent
booleans negotiated in the ACK, e.g. `sendControl` and `sendData`:

| Config | sendControl | sendData |
|---|---|---|
| Reactive only | true | false |
| Proactive only | false | true |
| Reactive + proactive | true | true |

This makes the experiment matrix explicit and removes the "always sent"
exception. (If you prefer an enum, add `AGENT_MODE_DATA_ONLY` — but two flags
compose better and avoid a 4th "neither" state needing validation.)

#### C4. Frame timer can die on the empty-frame fast path — ✅ RESOLVED (Piece 7)

> `handleSelfMessage("sendUserList")` reschedules unconditionally first, then
> calls `sendEventFrame()` / `sendDataFrame()` (both may no-op). Timer cannot die.

**Problem.** Piece 7 says `sendControlEvents()` should "return, no packet sent"
when both pending maps are empty, and separately "send, clear, **reschedule at
frameInterval_**." If the reschedule lives on the send path, an interval with no
changes returns early → the self-message is never rescheduled → the periodic
frame loop stops permanently after the first quiet interval.

**Proposed alternative.** Separate cadence from payload. In the timer handler:
1. **Always** `scheduleAt(simTime() + frameInterval_, ...)` first.
2. Then conditionally build/send the control frame (skip if both maps empty).
3. Then conditionally send the data frame (skip unless data enabled).
Make this explicit in Piece 7 so the reschedule is unconditional.

#### C5. A stale CONTROL_EXIT from the old MEH can delete a handed-over user — ✅ RESOLVED (Piece 10)

> `handleEventFrame` Step 4 skips any EXIT whose `sourceMEH != currentMEH` — only
> the user's current MEH can start an exit-hold. (Was missing in the first cut of
> Piece 10; added during the 2026-06-29 review.)

**Problem.** On handover A→B the Controller receives ENTRY from B and EXIT from
A, order not guaranteed (separate UDP sources). If the EXIT from A is processed
after the user's `currentMEH` is already B, the EXIT handler (Piece 10,
lines 426–429) marks the user for removal — deleting a user who is validly at B.

**Proposed alternative.** In the EXIT handler, ignore the event when its source
MEH ≠ the user's current `currentMEH` (it is a stale exit from the cell the user
already left). Only an EXIT from the user's *current* MEH counts as a departure.

### 🟠 Medium — correctness/clarity, not goal-breaking

- **M1. Relocated hidden constraint.** Even with C1's fix, `threshold_` must be
  safely larger than `frameInterval` or the safety-net purge fires during normal
  operation. eRAVENS set out to kill the `ttl_ + forceUpdate < HANDOVER_LOCKOUT`
  coupling; document the new `threshold_ ≫ frameInterval` relationship in one
  place (Piece 8/10) so it is not re-broken. Better: derive it (e.g.
  `threshold_ = k · frameInterval`) instead of an independent NED value.

- **M2. `firstDetectedAt` is carried but unused.** The Controller logic only
  reads `samplesSinceChange`. Either (a) use `firstDetectedAt` to log
  detection→action latency (genuinely useful for the research metrics), or
  (b) drop it from the wire to save bytes. Decide explicitly; right now it is
  dead payload.

- **M3. Controller presence-tracking differs per mode.** With data frames the
  Controller can rebuild the full user map; with control-only it must maintain
  the map purely from ENTRY/EXIT deltas. `updateUserStateMap()` (Piece 10) needs
  a clearly defined control-driven path, not just the data-frame path. Spell out
  how the map is seeded/maintained in CONTROL_ONLY.

- **M4. One timer, two frames — define lifecycle.** Piece 7 sends both from the
  `sendUserList` handler. Define explicitly: single self-message, reschedule
  once (see C4), control frame built first, data frame second, both independent
  of each other's presence.

### 🟡 Low — consistency, dead state, typos

- **L1. Stale architecture section.** "Agent internal LS tracking" (lines 96–106)
  still describes `presenceCount[]` / `pendingExits[]` as separate counters. The
  implemented design uses `pendingEntries_` / `pendingExits_` of
  `PendingEvent{firstDetectedAt, sampleCount}`. Rewrite to match Piece 6/7.

- **L2. Confirmation model restated inconsistently.** Lines 114–116 and
  Verification #7 describe the cross-frame model removed in C2. Update both.

- **L3. `INFRAESTRUCTURE` misspelling.** Pervasive in the protocol defines and
  message classes (should be `INFRASTRUCTURE`). It is internally consistent, so
  defer to the Piece 12 rename pass — but track it there so it is not forgotten.

- **L4. `localSnapshotCounter`.** Retained from the snapshot era; its meaning now
  straddles two frame types. Rename (e.g. `frameCounter_`) or drop if unused as a
  `requestId`.

- **L5. `int rate` resolution.** `rate` is integer milliseconds, so sub-ms frame
  intervals are unrepresentable and `frameInterval`s like 1.5s round-trip fine
  but 0.0005s would not. Acceptable given 1–5s targets — just note the
  granularity limit so nobody expects finer control later.

- **L6. AP-not-found path (pre-existing).** In `handleLSMessage`, a failed
  `apIndex_` lookup leaves `apData` default-constructed, so `accessPointId` is
  "" and distance is computed against (0,0). Not introduced by eRAVENS, but the
  diff logic now keys correctness on AP identity — worth a guard or at least an
  EV warning.

### Summary

The Agent-side delta model is sound and the bandwidth story is strong. The gaps
are concentrated on the **Controller's assumption of periodic full state**, which
the delta model removes. Resolving C1–C3 before Piece 10 is essential — they are
not edge cases, they directly determine whether the three experimental configs
produce correct handover behavior. C4/C5 are small code-shape fixes. Everything
in Medium/Low can be folded into the relevant pieces as they are implemented.

---

## Logic Faults — Agent/Controller code-level review

> Second review pass, grounded in a full read of `RavensControllerApp.cc`, the
> three handler policies (`NotifyOnDataChange`, `SendToExternalServer`,
> `SaveDataHistory`), `RavensAgentApp.cc`, and the Simu5G `LocationService`.
> These are about the *runtime behaviour* of the current code interacting with
> the eRAVENS design — distinct from the planning critique above.
>
> **Terminology note (R1):** old `UE_CONTROL_EVENT` / "control frame" names below
> map to `UE_EVENT` / "event frame" per R1a. **F3 (the `7` collision) is now
> resolved by R1a** (fresh value for `UE_EVENT`, MEO codes → 20/21). F1, F2, F4,
> F5 are runtime concerns unaffected by R1 and remain open.

### Context confirmed (not a fault, but load-bearing)

- **LS scoping is correct.** The Agent subscribes with `"cells":[0]`, and in
  `UsersListNotificationSubscription.cc:108` `0` means "all cells". BUT each MEC
  host runs its **own** `LocationService` scoped to its own eNodeBs
  (`LocationService.cc:51`, `addEnodeB(eNodeB_)`). So `[0]` = "all of *this
  host's* cells" → each Agent sees only its own users. The handover diff model
  is safe; there is **no** network-wide presence leak.
- **Radio layer guarantees single attachment.** A UE is in exactly one cell at a
  time, so it cannot appear in two MEHs' LS simultaneously.

### F1 — Removing the `-1` filter changes the "attached" criterion

Today four sites gate on `getDlNongbrDelayUe() == -1 → continue`
(`updateUserStateMap`, `NotifyOnDataChange`, `SendToExternalServer`,
`formatSnapshot`). That sentinel is the *de facto* LS↔RNIS synchronizer: a user
appears in LS first but is **invisible to the Controller until RNIS confirms
attachment**. eRAVENS drops per-user RNIS and this filter, so ENTRY now fires
the instant LS sees the user.

This is the intended "LS is the source of truth" behaviour, **but** it shifts
*when* entries/handovers fire and removes the implicit "has real radio data"
gate. Every experiment's timing baseline moves. Make it a conscious decision and
document it; do not let it happen silently inside Piece 11.

### F2 — Departure vs handover cannot be told apart without a hold window — ✅ RESOLVED (Piece 10)

> Implemented via `UserState.pendingExitTime`. A confirmed EXIT (from the current
> MEH, per C5) sets `pendingExitTime = simTime() + frameInterval_` instead of
> removing the user. If an ENTRY for the same UE from a different MEH arrives
> before the hold expires → reclassified as HANDOVER (hold cancelled). Otherwise
> the next `handleEventFrame` purges the user (Step 1). One localized timer in one
> place, exactly as proposed below.

**This was the most important runtime fault and it is in direct tension with C1
and C5 — they were reconciled together (see above).**

On an A→B handover the Controller receives **EXIT from A** and **ENTRY from B**
as two independent UDP packets, order not guaranteed.

- C5 covers *EXIT after ENTRY* → ignore the stale exit (source MEH ≠ currentMEH).
- F2 is the **reverse**: *EXIT from A arrives before ENTRY from B*. If EXIT is
  authoritative (C1) and removes the user immediately, then B's ENTRY sees no
  user → emits a **cold ENTRY** instead of a **handover/migration**. The MEO
  then tears down and cold-starts the app instead of migrating live state.

The **old** design avoided this for free: EXIT only came from the inactivity
timeout, by which time B's snapshots had already moved `currentMEH` to B. Event-
based EXIT is faster but loses that natural buffer. **This is the real reason
`exitTTL` / `bufferTime` existed.**

`samplesSinceChange` does **not** solve this — it is per-MEH and cannot order
events across two different MEHs.

**Required reconciliation (C1 + C5 + F2):**
- Keep CONTROL_EXIT as the *trigger*, but **do not emit EXIT to the MEO
  immediately**. Hold it for a short window (≈ 1 frame interval) at the
  Controller. If an ENTRY for the same UE from a different MEH arrives within the
  window → reclassify as **handover/migration**. Otherwise → emit **departure**.
- This is a small, bounded Controller-side timer (a re-introduced but *localized*
  `exitHold`), not the old global `HANDOVER_LOCKOUT` coupling. It lives in one
  place (the EXIT path) and is the *only* timing parameter the Controller needs.
- This supersedes the plan's "no Controller timer at all" claim. The honest
  statement is: the Controller needs exactly one short hold window to merge
  EXIT+ENTRY into a handover; everything else is event-driven.

### F3 — `type` code 7 is double-defined — ✅ RESOLVED by R1a

`UE_EVENT` (was `UE_CONTROL_EVENT = 7`, Agent→Controller) collided by value with
`#define USERS_UPDATE 7` (`RavensControllerApp.cc:12`, Controller→MEO). Different
gates/sockets so no wire collision *today*, but two names = `7` visible in the
same translation unit is a latent trap, especially once `socketDataArrived`
switches on type `7` for incoming event frames. **R1a/Piece 12 fold in the fix:**
`UE_EVENT` keeps a unique data-plane value and the MEO-facing codes
(`USERS_UPDATE`, `MIGRATION_PLAN`) are renumbered to 20/21.

### F4 — Reactive detection latency is three-staged

Total reactive handover latency =
`LS detect (≤1s)` + `frameInterval` (Agent batching) +
`snapshot_frequency_` (Controller→MEO batching, `RavensControllerApp.cc:162`).
The Controller's MEO-reporting cadence (`snapshot_frequency_`) is **independent**
of the Agent's `frameInterval`. Confirm this is intended and account for all
three terms when reporting handover latency in the research results.

### F5 — Flask `APId → MEH` mapping assumes 1 cell = 1 MEH

`SendToExternalServer.cc:221` builds `"mecHost" + AccessPointId`. The moment a
MEH serves multiple cells (which the new cell-level aggregation contemplates),
this mapping breaks. Pre-existing, but newly relevant once aggregation lands.

---

## Loose Ends — won't-compile / dead-code surface

> Dangling threads from the half-finished Agent migration. Expected, but listed
> so none slip through. Each maps to a remaining Piece. Identifiers shown are
> what's **currently in the code** (old names); "replace with" targets use the
> post-R1 names.

**Agent (`RavensAgentApp.cc`) — Piece 7:**
- `sendUsersInfoSnapshot()` (≈ lines 167–233) still references removed members:
  `ttl_`, `hasPendingUpdates_`, `lastSentTimestamp_`, `forceUpdateInterval_`,
  `getRetrievalInterval()`, `UserData::getLastUpdated()`. Dead + breaks build —
  replace with `sendEventFrame()` + `sendDataFrame()`.
- `handleRNISMessage()` (≈ 540–549) still calls `setDlNongbrDelayUe(...)` and
  siblings, `setRnisUpdate()`, `setLastUpdated()`, `hasPendingUpdates_` — all
  removed. Replace with cell-level aggregation into `AccessPointRadioInfoData`.
- `handleSelfMessage()` "sendUserList" branch (≈ 419) still calls
  `sendUsersInfoSnapshot()`.
- `LETS PRINT THE USERS` debug loop (≈ 749–752) — remove.

**Controller (`RavensControllerApp.cc`) — Pieces 9–10:**
- Still uses `RavensLinkUsersInfoSnapshotMessage` / `USERS_INFO_SNAPSHOT`
  (`socketDataArrived` ≈ 226–228, `updateUserStateMap`, `updateMehStateMap`) —
  renamed to `RavensLinkDataFrameMessage` / `DATA_FRAME` in Piece 2, so it will
  not link until updated.
- `sendInfrastructureDetailsAck()` (≈ 249–261) hardcodes `setRate(3000)` and
  never calls `setAgentMode(...)`.
- No `UE_EVENT` branch in `socketDataArrived` yet (needs `handleEventFrame()`).
- `shouldAcceptHandover()` + `HANDOVER_LOCKOUT`/`MIN_UPDATE_INTERVAL` still
  present; to be removed/replaced by the confidence-threshold + exit-hold model.
- No TCP control plane yet (R1c): no listening `serverSocket`, handshake still
  handled on the UDP path.

**Handler policies — Piece 11:**
- `formatSnapshot()` (`SendToExternalServer.cc` ≈ 149–167) reads
  `getLastUpdated()`, `getLsUpdate()`, `getRnisUpdate()` and all 6 per-user RNIS
  getters — all removed from `UserData`.
- All three policies gate on `getDlNongbrDelayUe() == -1` (see F1) — decide the
  replacement "attached" criterion before removing.

**Dead state (Piece 12):**
- `update` member (`inet::Packet*`) — always `nullptr` (handlers return
  `nullptr`).
- `calculateAvg_` — TODO, never scheduled.
- `localSnapshotCounter` — leftover from the snapshot era; rename to
  `frameCounter_` or drop.

---

## Status snapshot for resuming on another machine

> Updated 2026-06-29 (second pass) after Piece 13, the **D1 confidence-gate
> removal**, the Agent sampleCount accumulation fixes, and timer-driven hold
> expiry. Branch layout: `eRavens/agent`, `eRavens/controller`, `eRavens/main`.

**Done:**
- **Pieces 1–6** — defines, message classes, `UserData`, `AccessPointRadioInfoData`,
  Agent `.ned`, Agent `.h`.
- **Piece 7** (`RavensAgentApp.cc`) — `handleRNISMessage` aggregation,
  `sendEventFrame()`, `sendDataFrame()`, `handleSelfMessage` dispatch (C4).
  **+ two sampleCount accumulation fixes** (see B1 below): ENTRY count now bumps in
  the existing-user branch; EXIT count accumulated by a pass over `pendingExits_`
  before the departure loop. **+ `numberOfActiveUeDlNongbrCell` now populated**
  (was silently 0).
- **Piece 8** (`RavensControllerApp.ned`) — `frameInterval`, `threshold` params;
  ports `dataPort`(UDP 5001)/`mgmtPort`(TCP 5000). Agent side:
  `controllerDataPort`/`controllerMgmtPort`. **`confirmationCount` /
  `exitConfidenceThreshold` removed (D1).**
- **Piece 9** (`RavensControllerApp.h`) — `UserState`: `pendingExitTime`,
  `pendingExitSamples`, `pendingExitFirstAt`. `expireHoldsMsg_` timer member.
- **Piece 10 + D1** (`RavensControllerApp.cc`) — `handleEventFrame()`:
  (1) `expirePendingExits()`, (2) **split events by type — no confidence gate**,
  (3) EXIT starts F2 hold only if source==currentMEH (C5) + stashes metadata,
  (4) ENTRY = new user / handover (both arrival orders) / flap-noop.
  `updateUserStateMap` **only refreshes known users — does NOT seed the map** (so a
  DATA_FRAME can't pre-empt the ENTRY hook). `shouldAcceptHandover` deleted.
- **Piece 13** — event→MEO via 3 semantic hooks (`onUserEntry/Handover/Exit`) on
  `LocationDataHandlerPolicyBase` (+ new `.cc` with `emitUserUpdate`). All three
  policies override them; SaveDataHistory writes lifecycle CSV only (MEO-silent).
- **Piece 11 bodies** — `SendToExternalServer::formatSnapshot` rewritten to
  cell-aggregate JSON (`cellMetrics` + LS-only `users`); `SaveDataHistory`
  `radio_stats.csv` extended to full cell-metric set; user CSV stripped to LS fields.
- **Timer-driven hold expiry** — `expirePendingExits()` called both inline from
  `handleEventFrame` (prompt) and from a periodic `expireHolds` self-message at
  `frameInterval_` (liveness in quiet regions / EVENT_ONLY).
- **R1a/R1b/R1c** — rename, Agent TCP mgmt socket, Controller TCP server.

**Resolved critique/fault items:** C1 (EXIT authoritative; `removeInactiveUsers`
is a safety net, valid only in EVENT_AND_DATA — see L7), **C2 → superseded by D1
(confidence gate removed entirely; presence is unconditional, debounce is the F2
hold alone)**, C4, C5 (stale-EXIT guard), F2 (`pendingExitTime` hold), F3, M2
(`firstDetectedAt` logged).

**New this session:**
- **B1 (bug, fixed).** Agent `samplesSinceChange` was stuck at 1 forever (ENTRY
  increment unreachable after first sample; EXIT loop never revisited removed
  users). Every event carried count 1 → with the old gate at 2, everything was
  filtered. Fixed both accumulation paths. The count is now accurate **metadata**
  (it no longer gates anything after D1).
- **D1 (design, done).** Removed the ENTRY/EXIT confidence threshold. Rationale:
  LS is the single source of truth for presence, so a "confidence gate" has nothing
  to filter — every LS appearance is a real attachment. Under the delta /
  report-once model, gating with a threshold > 1 *permanently lost* any entry whose
  first sample happened to land in the last sub-frame window (~1/3 of entries). The
  F2 hold already provides the only debounce that matters (handover vs departure,
  across two Agents). So: accept all presence; hold disambiguates; counts are
  metadata. One timing knob left (hold = `frameInterval`).

**Still open / not done:**
- **C3 — proactive-only config still not expressible.** `agentMode` is a 2-value
  enum (`EVENT_ONLY` / `EVENT_AND_DATA`); no DATA-without-EVENT mode. The
  two-boolean (`sendEvent`/`sendData`) redesign is not done.
- **L7 (new) — EVENT_ONLY has no lost-packet safety net, by construction.**
  `removeInactiveUsers()` keys on `timestamp` staleness, which only refreshes via
  data frames; in EVENT_ONLY a stable present user never refreshes, so the safety
  net cannot run there without wrongly purging present users (C1). Reactive-mode
  departure correctness therefore rests entirely on the EXIT frame being delivered
  (lossless wired backhaul in sim → safe). **State this assumption explicitly in
  the thesis when describing the reactive config.**
- **L8 (new) — `numberOfActiveUeDlNongbrCell` is a derived proxy.** RNIS does not
  expose it directly; it is set to the per-UE aggregation `count` (UEs with
  non-GBR activity this notification). Document the exact definition with the
  metric schema; it is not necessarily identical to "active UEs" as the name implies.
- **Piece 12** — dead-code cleanup, `INFRAESTRUCTURE`→`INFRASTRUCTURE` rename,
  `localSnapshotCounter`→`frameCounter_`, F4/F5 documentation.

The tree does not compile until built once (stale `_m.h` regenerate from
`RavensLinkPacket.msg` via `opp_msgc` at build time).
