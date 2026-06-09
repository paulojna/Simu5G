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

- **Control frames** — *what changed*: UE ENTRY/EXIT events, derived by the Agent
  diffing the LS user list at 1 s granularity, annotated with a confidence
  signal (`samplesSinceChange`). Sent only when something changes.
- **Data frames** — *features*: LS-derived user state + **cell-level aggregated**
  RNIS (not per-user). Periodic, only in modes that need it.

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
   "data without control" (see C3). The comparison is the deliverable.
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
| No distinction between data and control traffic | Single frame type serves all purposes |

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
   control frame at `frameInterval` (e.g. 3s), with confidence context.
3. **RNIS aggregation** — per-user radio stats (N users × 6 fields) aggregated
   to cell-level metrics (6 fields total) before transmission.

Result: Agent→Controller bandwidth is a fraction of naive forwarding.
The `SaveDataHistory` / `SendToExternalServer` modes log and measure this,
enabling quantitative comparison across experimental configurations.

---

### Two frame types

**Control frame** (`UE_CONTROL_EVENT`) — periodic, confidence-annotated:
- Sent every `frameInterval` (e.g. 3s), always, regardless of agent mode
- Carries a **batch** of user state changes since last frame, each annotated with:
  - `eventType`: ENTRY or EXIT
  - `samplesSinceChange`: how many consecutive 1s LS samples the user has been
    in this state (e.g. present for 3 samples = confident; absent for 1 = uncertain)
  - `firstDetectedAt`: simtime when the state change was first seen
- Only changed users are reported — stable users are omitted from control frames
- The Controller uses `samplesSinceChange` to make handover/departure decisions
  without needing its own per-user history or a separate exitTTL timer

**Data frame** (`DATA_FRAME`) — periodic, full state:
- Sent every `frameInterval`, only when `agentMode = AGENT_MODE_CONTROL_AND_DATA`
- LS-derived user map: address, AP, location, speed, bearing, distance
- RNIS-derived cell aggregates: PRB usage, avg delay, PDR, data volume, avg distance
- No per-user RNIS fields

### Agent mode (set by Controller via INFRAESTRUCTURE_DETAILS_ACK)

| Mode | Agent sends | Used by |
|---|---|---|
| `AGENT_MODE_CONTROL_ONLY (0)` | Control frames only | NotifyOnDataChange |
| `AGENT_MODE_CONTROL_AND_DATA (1)` | Control + Data frames | SaveDataHistory, SendToExternalServer |

### Registration flow (unchanged)

```
Agent                          Controller
  |                                |
  |--- JOIN_NETWORK_REQUEST ------>|
  |<-- JOIN_NETWORK_ACK -----------|
  |--- INFRAESTRUCTURE_DETAILS --->|  (AP list)
  |<-- INFRAESTRUCTURE_DETAILS_ACK-|  (agentMode + frameInterval in ms)
  |                                |
  |--- [subscribes to LS + RNIS] --|  (internal, no Controller involvement)
  |                                |
  |=== operational phase ==========|
  |--- UE_CONTROL_EVENT ---------->|  (every frameInterval, all modes)
  |--- DATA_FRAME ---------------->|  (every frameInterval, mode=1 only)
```

### Agent internal LS tracking (between frame sends)

The Agent sees LS notifications every 1s. Between control frame sends it:
- Tracks `presenceCount[ueAddr]` — consecutive samples user has been present
- Tracks `pendingExits[ueAddr]` — users absent from LS, with absence count and
  first-absent simtime
- On each LS notification:
  - Users present: increment `presenceCount`, remove from `pendingExits` if there
  - Users absent (were in map before): move to `pendingExits`, increment absence count
- At control frame time: report all entries with their `presenceCount` and all
  pending exits with their absence count. Clear `pendingExits` after reporting.

### Handover detection (Controller)

```
Control frame from MEH-B: user ENTRY, samplesSinceChange=3
  → user has been at MEH-B for 3 confirmed samples → HANDOVER CONFIRMED immediately

Control frame from MEH-B: user ENTRY, samplesSinceChange=1
  → user just appeared at MEH-B → start confirmation (pendingMEH=B, pendingCount=1)
  → on next control frame from MEH-B with same user → HANDOVER CONFIRMED

Control frame from MEH-A: user EXIT, samplesSinceChange=1
  → user absent only 1 sample → Controller may wait (low confidence)

Control frame from MEH-A: user EXIT, samplesSinceChange=3
  → user absent 3 samples → confident departure

No agent reports user for threshold_ seconds:
  → removeInactiveUsers() → EXIT
```

The `samplesSinceChange` field replaces the Controller's `exitTTL` timer.
The Agent already did the counting; the Controller just reads the result.

### Research comparison

eRAVENS supports three experimental configurations:

1. **Reactive only** — `NotifyOnDataChange` mode, control frames only
   - Handover detected within `frameInterval` (1–5s configurable)
   - Always correct, always after the fact
   - Minimum bandwidth

2. **Proactive only** — `SendToExternalServer` mode, data frames only (no control)
   - ML predicts handovers before they happen using cell-level aggregates + LS state
   - May miss handovers, no safety net

3. **Reactive + Proactive** — `SendToExternalServer` mode, control + data frames
   - ML predicts early, control frames catch what ML misses
   - Best latency, zero missed handovers

The bandwidth consumed by each configuration is measured via OMNeT++ signals
(bytes per frame type per simulation second), enabling direct quantitative
comparison.

---

## Implementation Plan

### ✅ Piece 1 — Defines (`RavensAgentApp.h`, `RavensControllerApp.h`)

**Status: DONE**

Changed in both files:
- Removed `SET_RETRIEVAL_INTERVAL (4)` and `SET_RETRIEVAL_INTERVAL_ACK (5)` (unused)
- Renamed `USERS_INFO_SNAPSHOT (6)` → `DATA_FRAME (6)`
- Added `UE_CONTROL_EVENT (7)`
- Added `AGENT_MODE_CONTROL_ONLY (0)` and `AGENT_MODE_CONTROL_AND_DATA (1)`
- Added `CONTROL_ENTRY (0)` and `CONTROL_EXIT (1)`
- Removed `CHANGE_ENTRY/MEH/POSITION/EXIT/NO_CHANGE` from Controller header
  (internal state labels, belong in handler logic not wire protocol)

---

### ✅ Piece 2 — Message classes (`RavensLinkPacket.msg`)

**Status: DONE**

- Renamed `RavensLinkUsersInfoSnapshotMessage` → `RavensLinkDataFrameMessage`
  - Fields unchanged: `mecHostId`, `UsersMap users`, `AccessPointRNISData apRadioInfo`
- Updated `RavensLinkInfrastructureDetailsMessageAck`:
  - Kept `int rate` (frame interval in ms — same rate for control and data frames)
  - Added `int agentMode` (AGENT_MODE_CONTROL_ONLY or AGENT_MODE_CONTROL_AND_DATA)
- Added `RavensControlEvent` struct and `RavensControlEventList` typedef in
  the `cplusplus {{ }}` block (consistent with `@existingClass` pattern):
  ```cpp
  struct RavensControlEvent {
      std::string        ueAddress;
      int                eventType;           // CONTROL_ENTRY=0 or CONTROL_EXIT=1
      int                samplesSinceChange;  // consecutive 1s LS samples in this state
      omnetpp::simtime_t firstDetectedAt;     // simtime when change was first observed
  };
  typedef std::vector<RavensControlEvent> RavensControlEventList;
  ```
  Note: `RavensControlEvent` / `RavensControlEventList` are in the **global
  namespace** (not `simu5g::`) — reference without namespace prefix in C++ code.
- Added `RavensLinkControlEventMessage extends RavensLinkPacket`:
  ```
  string mecHostId
  RavensControlEventList events   // batch: all state changes this frame interval
  ```
  Sent at `frameInterval` **only when entries or exits exist**. No packet if
  no changes occurred. `exitTTL` is NOT in the message — the Controller uses
  `samplesSinceChange` instead of a local timer.

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

**Status: DONE**

Removed: `sendInterval`, `forceUpdateInterval_`, `lastSentTimestamp_`, `ttl_`,
`hasPendingUpdates_`, `getRetrievalInterval()`, `setRetrievalInterval()`.

Added:
```cpp
simtime_t frameInterval_;   // negotiated with Controller, used for both frame types
int agentMode_;             // AGENT_MODE_CONTROL_ONLY or AGENT_MODE_CONTROL_AND_DATA

// Pending control events — accumulated between frame sends, cleared after each frame
struct PendingEvent {
    simtime_t firstDetectedAt;
    int       sampleCount;
};
std::unordered_map<std::string, PendingEvent> pendingEntries_; // appeared since last frame
std::unordered_map<std::string, PendingEvent> pendingExits_;   // absent since last frame
```

Renamed `sendUsersInfoSnapshot()` → `sendDataFrame()`. Added `sendControlEvents()`.

---

### Piece 7 — `RavensAgentApp.cc` (IN PROGRESS)

**File:** `src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.cc`

#### ✅ Done

**Constructor**: Removed `sendInterval` initialization.

**`initialize()`**: Removed `ttl_`, `forceUpdateInterval_`, `lastSentTimestamp_`,
`hasPendingUpdates_`. Reads `frameInterval_` from NED. Defaults `agentMode_` to
`AGENT_MODE_CONTROL_AND_DATA` until ACK received.

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

**`sendControlEvents()`** — new method:
- If `pendingEntries_` and `pendingExits_` both empty → return, no packet sent
- Otherwise build one `RavensLinkControlEventMessage` with all events as
  `RavensControlEvent` structs (ENTRY and EXIT with `sampleCount` + `firstDetectedAt`)
- Send, clear both maps, reschedule at `frameInterval_`

**`sendDataFrame()`** — replaces `sendUsersInfoSnapshot()`:
- Skip if `agentMode_ != AGENT_MODE_CONTROL_AND_DATA`
- Compute `avg_distance_to_ap` from `users` map, call setter on `accessPointRadioInformation`
- Build and send `RavensLinkDataFrameMessage`
- Called from same timer handler as `sendControlEvents()`

**`handleSelfMessage()`** — update timer dispatch:
- `sendUserList` message → call `sendControlEvents()` then `sendDataFrame()`
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
int confirmationCount = default(2);      // min consecutive control frames from new MEH
                                          // before handover is accepted
double frameInterval = default(3);       // data frame interval sent to Agents via ACK (s)
int exitConfidenceThreshold = default(2); // min samplesSinceChange on EXIT to act on it
```

Note: `exitTTL` (a timer) is NOT added. The Agent's `samplesSinceChange` field
on EXIT frames replaces it — the Controller reads confidence from the data,
not from a local stopwatch.

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
    int pendingConfirmations;          // consecutive ENTRY frames from pendingMEH
    // REMOVED: simtime_t lastHandoverTime  (replaced by confirmation counter)
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
void handleControlEvent(inet::Ptr<const RavensLinkControlEventMessage> event,
                        inet::L3Address remoteAddress, int srcPort);
```

---

### Piece 10 — `RavensControllerApp.cc`

**File:** `src/apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.cc`

**`initialize()`**:
- Read `confirmationCount_`, `exitConfidenceThreshold_`, `frameInterval_` from NED

**`sendInfrastructureDetailsAck()`**:
- `setRate((int)(frameInterval_ * 1000))` — send interval in ms
- `setAgentMode(...)` based on configured handler policy

**`socketDataArrived()`**:
- Add branch for `UE_CONTROL_EVENT` → call `handleControlEvent()`
- Rename `USERS_INFO_SNAPSHOT` references to `DATA_FRAME`

**`handleControlEvent()`** — new method:

```
On CONTROL_ENTRY (samplesSinceChange = N):
  - If user not in map → new entry (signal handler policy)
  - If user in map, same MEH → refresh timestamp, reset pending state
  - If user in map, different MEH:
      - if pendingMEH != srcMEH: reset pendingMEH = srcMEH, pendingConfirmations = N
      - if pendingMEH == srcMEH: pendingConfirmations += N
      - if pendingConfirmations >= confirmationCount_ → HANDOVER CONFIRMED
        (signal handler policy, update currentMEH)

On CONTROL_EXIT (samplesSinceChange = N):
  - If N < exitConfidenceThreshold_: low confidence, Controller may log but not act
  - If N >= exitConfidenceThreshold_: departure is likely, mark for removal
    (removeInactiveUsers() will confirm via threshold_ timeout)
```

**`updateUserStateMap()`**:
- Now only updates `userData` content on DATA_FRAME (location, RNIS aggregates)
- Handover/departure decisions are driven by `handleControlEvent()`, not this method
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
- Entry/exit/handover now arrive via `handleControlEvent()` path

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
- Remove `USERS_UPDATE` / `MIGRATION_PLAN` references that need updating
  after message rename
- Clean up any remaining `USERS_INFO_SNAPSHOT` string references in EV logs

---

## Before vs. After

| Aspect | RAVENS | eRAVENS |
|---|---|---|
| **Control frame timing** | No control frame — one snapshot type | Periodic at `frameInterval` (1–5s, configurable) |
| **Departure detection** | TTL purge at snapshot time (up to 10s late) | LS diff at 1s, reported at next frame |
| **Confidence signal** | None — Controller uses time-based lockout | `samplesSinceChange` per event — Agent already did the counting |
| **Handover logic** | `HANDOVER_LOCKOUT = 10s` hardcoded timer | Confirmation counter (N consecutive ENTRY frames from new MEH) |
| **exitTTL** | Implicit in HANDOVER_LOCKOUT | Replaced by `exitConfidenceThreshold_` on `samplesSinceChange` field |
| **Per-user RNIS** | 6 fields per user in every snapshot | None — dropped entirely |
| **Cell-level metrics** | 4 fields (PRB, PDR) | 4 existing + 5 new aggregates |
| **MEH compute** | Minimal (LS polling only) | LS diff, sample counting, RNIS aggregation |
| **Rate negotiation** | Broken (hardcoded + integer division) | Fixed: NED param on Controller, correct float conversion |
| **Agent mode** | Single mode | CONTROL_ONLY or CONTROL_AND_DATA |
| **Hidden constraints** | `ttl_ + forceUpdate < HANDOVER_LOCKOUT` | None |

---

## Verification

After all pieces are implemented:

1. **Compile** — project builds cleanly
2. **Control frame content** — EV log shows `samplesSinceChange` increments correctly across LS cycles
3. **Departure detection** — Agent removes departed user from map within 1s, reports EXIT in next control frame with correct `samplesSinceChange`
4. **No stale users** — Agent never sends a departed UE in a data frame
5. **Mode enforcement** — CONTROL_ONLY Agent sends no data frames
6. **Rate negotiation** — Agent adopts Controller's `frameInterval` correctly (no integer division)
7. **Confirmation counter** — Controller accepts handover after N ENTRY frames with sufficient `samplesSinceChange`
8. **Low-confidence exit** — EXIT with `samplesSinceChange=1` does not immediately trigger departure in Controller
9. **Cell aggregates** — `AccessPointRadioInfoData` carries correct avg/total values
10. **Flask format** — `SendToExternalServer` JSON matches new schema

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
| `RavensAgentApp.cc` | Piece 7 — in progress |
| `RavensControllerApp.ned` | Piece 8 |
| `RavensControllerApp.h` | Piece 9 |
| `RavensControllerApp.cc` | Piece 10 |
| `NotifyOnDataChange.cc/.h` | Piece 11 |
| `SendToExternalServer.cc` | Piece 11 |
| `SaveDataHistory.cc` | Piece 11 |
| Dead code | Piece 12 |

---

## Plan Review (pre-implementation critique)

> Added as a review pass. Nothing below is implemented — these are issues found
> while re-reading the plan against the current Controller code. Ordered by
> severity. Each item has a proposed alternative.

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

#### C2. Cross-frame confirmation counter is incompatible with "report entry once"

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

#### C4. Frame timer can die on the empty-frame fast path

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

#### C5. A stale CONTROL_EXIT from the old MEH can delete a handed-over user

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

### F2 — 🔴 Departure vs handover cannot be told apart without a hold window

**This is the most important runtime fault and it is in direct tension with C1
and C5 above — they must be reconciled together.**

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

### F3 — `type` code 7 is double-defined

`UE_CONTROL_EVENT = 7` (header, Agent→Controller) collides by value with
`#define USERS_UPDATE 7` (`RavensControllerApp.cc:12`, Controller→MEO). Different
gates/sockets so no wire collision *today*, but two names = `7` visible in the
same translation unit is a latent trap, especially once `socketDataArrived`
switches on type `7` for incoming control frames. Renumber the MEO-facing codes
(`USERS_UPDATE`, `MIGRATION_PLAN`) to a non-overlapping range.

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
> so none slip through. Each maps to a remaining Piece.

**Agent (`RavensAgentApp.cc`) — Piece 7:**
- `sendUsersInfoSnapshot()` (≈ lines 167–233) still references removed members:
  `ttl_`, `hasPendingUpdates_`, `lastSentTimestamp_`, `forceUpdateInterval_`,
  `getRetrievalInterval()`, `UserData::getLastUpdated()`. Dead + breaks build —
  replace with `sendControlEvents()` + `sendDataFrame()`.
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
- No `UE_CONTROL_EVENT` branch in `socketDataArrived` yet (needs
  `handleControlEvent()`).
- `shouldAcceptHandover()` + `HANDOVER_LOCKOUT`/`MIN_UPDATE_INTERVAL` still
  present; to be removed/replaced by the confirmation-count + exit-hold model.

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

**Done:** Pieces 1–6 (defines, message classes, `UserData`,
`AccessPointRadioInfoData`, Agent `.ned`, Agent `.h`).

**In progress:** Piece 7 (`RavensAgentApp.cc`) — constructor, `initialize()`,
`handleProcessedMessage` (ACK), and `handleLSMessage` are done and documented.
**Remaining in Piece 7:** `handleRNISMessage` aggregation, `sendControlEvents()`,
`sendDataFrame()`, `handleSelfMessage` dispatch, delete `sendUsersInfoSnapshot()`.

**Not started:** Pieces 8–12 (entire Controller side + handler policies + cleanup).

**Before writing Piece 10, settle the design questions C1, C2, C3 and F2** — they
determine whether handover / departure / migration are correctly distinguished.
The current tree does **not** compile (expected) — see Loose Ends above.
