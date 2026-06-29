# RAVENS — System Flow & Modes (reading guide)

> A walkthrough of how RAVENS works end-to-end, mode by mode, with the actual
> method names so you can read it alongside the code. For *why* the design is the
> way it is, see `eRAVENS_PLAN.md` (esp. Design Revision **D1** and critique
> **C1/C2/C5/F2**). This file is the *what happens, in what order*.

Paths are relative to `src/apps/mec/RavensApps/`.

---

## 1. The cast

| Actor | Where | Job |
|---|---|---|
| **RavensAgentApp** | `RavensAgentApp/` — one per MEC host | Watches the local MEC platform services (LS + RNIS), diffs presence locally, reports deltas + features to the Controller. |
| **RavensControllerApp** | `RavensControllerApp/` — one, on `server` | Keeps the global picture (`userStateMap`, `mehStateMap`), runs one **policy**, and drives the MEO. |
| **Handler policy** | `RavensControllerApp/LocationDataHandlerPolicies/` | The *strategy*. One of three. Decides what to do with the global picture (log it / react / predict). |
| **MEO** (`MecOrchestrator`) | `nodes/mec/MECOrchestrator/` | Instantiates / migrates / terminates the actual MEC apps. RAVENS only feeds it; it acts. |

The Agent–Controller link is the focus. The Controller–MEO link is one message type
(`USERS_UPDATE`) plus, in proactive mode, `MIGRATION_PLAN`.

---

## 2. Two planes, two frame types

**Control plane (TCP, one-time, mgmt port 5000)** — the handshake that registers an
Agent and tells it how to behave:

```
Agent  --JOIN_NETWORK_REQUEST-->        Controller   (Agent announces its mecHostId)
       <--JOIN_NETWORK_ACK--------
       --INFRAESTRUCTURE_DETAILS-->                   (Agent sends its AP/cell list)
       <--INFRAESTRUCTURE_DETAILS_ACK--               (Controller replies: frameInterval + agentMode)
   [Agent closes the TCP connection]
```

The ACK is built in `RavensControllerApp::sendInfrastructureDetailsAck()` — it sets
`rate` (= `frameInterval * 1000` ms) and `agentMode`. **`agentMode` is derived from
the Controller's configured `mode`:** `NotifyOnDataChange` → `EVENT_ONLY`, everything
else → `EVENT_AND_DATA`.

**Data plane (UDP, periodic, data port 5001)** — the telemetry:

| Frame | Type code | Carries | Sent when |
|---|---|---|---|
| **Event frame** | `UE_EVENT` | batch of ENTRY/EXIT deltas, each with `samplesSinceChange` + `firstDetectedAt` | every `frameInterval`, **all modes**, only if something changed |
| **Data frame** | `DATA_FRAME` | full LS user map + **cell-level** RNIS aggregates | every `frameInterval`, **only in `EVENT_AND_DATA`** |

Both are sent from `RavensAgentApp::sendEventFrame()` / `sendDataFrame()`, dispatched
by the `sendUserList` self-message in `handleSelfMessage()` (which reschedules
itself *first*, then sends — so the cadence can't die on a quiet interval).

---

## 3. How the Agent produces events (the local diff)

The Agent subscribes to LS and gets a user list **every 1 s** (`handleLSMessage()`).
Between frame sends it keeps two maps of `PendingEvent{firstDetectedAt, sampleCount}`:

- `pendingEntries_` — users that appeared since the last frame
- `pendingExits_` — users that vanished since the last frame

On each 1 s LS notification:
- A **new** user → added to `users`, seeded into `pendingEntries_` (count 1).
- A user **still present** with a pending entry → its `sampleCount++` (accumulating confidence).
- A user **absent** → removed from `users`, seeded into `pendingExits_`; a separate
  pass bumps `sampleCount` for exits that stay absent across samples.
- A reappearing user cancels its pending exit, and vice-versa.

At frame time `sendEventFrame()` emits everything in both maps (as `RavensEvent`s)
and clears them. **Report-once:** once reported, a stable user is silent forever —
that's the bandwidth win, and the reason `samplesSinceChange` is *metadata*, not a
gate (see D1).

`handleRNISMessage()` aggregates per-UE RNIS into **cell-level** numbers in
`AccessPointRadioInfoData` (avg delays, total data volumes, PRB usage, PDR, active-UE
count, avg distance). No per-user RNIS leaves the Agent.

---

## 4. The Controller's shared pipeline (runs in every mode)

Entry point: `RavensControllerApp::socketDataArrived(UdpSocket*)`.

### 4a. Event frames → `handleEventFrame()`  ← the heart of the system

1. **`expirePendingExits()`** — any user whose F2 hold has elapsed → fire
   `onUserExit` hook, remove from `userStateMap`. (Also runs on a timer, see §6.)
2. **Split** events into entries / exits. **No confidence filter** (D1) — LS is the
   source of truth for presence, so every event is acted on.
3. **Exits** → if the EXIT's source == the user's current MEH, **open an F2 hold**
   (`pendingExitTime = now + frameInterval`) and stash the event's `samplesSinceChange`
   / `firstDetectedAt`. If source ≠ current MEH it's a **stale exit** from a cell the
   user already left → ignored (**C5 guard**).
4. **Entries** → three cases:
   - unknown user → create in `userStateMap`, fire **`onUserEntry`**
   - user in an exit-hold → cancel hold; if new MEH ≠ old → **`onUserHandover`**; if
     same MEH → flap, no hook
   - user already active at a different MEH (ENTRY arrived before the EXIT) →
     **`onUserHandover`** directly

The result: exactly one semantic hook per real transition, in **either** arrival
order, and never a spurious A→A handover.

### 4b. Data frames → `updateUserStateMap()` + `updateMehStateMap()` + policy

- `updateUserStateMap()` **only refreshes users it already knows** — it does **not**
  create them. Presence is owned by `handleEventFrame`; if a DATA_FRAME could seed
  the map it would pre-empt the ENTRY and the `onUserEntry` hook would never fire.
- `updateMehStateMap()` stores the cell-level RNIS for the host.
- Then the policy's **`handleDataMessage()`** runs (this is where each mode does its
  data-frame work — log / forward to ML).

### 4c. Controller → MEO  (`handleSelfMessage("sendSnapshot")`, every `snapshot_frequency`)

Flushes two maps to the MEO over `outGate`:
- `userUpdates` (`UserMEHUpdate`) → `USERS_UPDATE` message
- `migrationPredictions` → `MIGRATION_PLAN` message (proactive only)

`userUpdates` is filled **only** by the policy hooks calling
`emitUserUpdate(address, lastMEH, newMEH)` (in `LocationDataHandlerPolicyBase`).
A policy that never calls it is MEO-silent.

---

## 5. The three modes

All three share §3 and §4. They differ only in **`agentMode`** and in **what the
policy hooks + `handleDataMessage` do**.

### 5a. `SaveDataHistory` — dataset logging (MEO-silent)

- **agentMode:** `EVENT_AND_DATA` (wants the full feature stream).
- **Hooks** (`onUserEntry/Handover/Exit`) → write one row to `*_lifecycle.csv`
  (`timestamp,eventType,userId,fromMEH,toMEH,samplesSinceChange,firstDetectedAt`).
  **No `emitUserUpdate`** → the MEO is never touched.
- **`handleDataMessage()`** → writes per-user LS rows to `*_users.csv` and cell
  aggregates to `*_radio_stats.csv`; also runs the safety-net purge (logged as EXIT
  with `samples = -1`).
- **Why:** produces the labelled dataset (lifecycle = ground-truth transitions;
  radio_stats/users = features) to train/evaluate the proactive model offline.
- **Nuance:** because it's MEO-silent, app placement does **not** follow the UE here.
  Use `selectionPolicy = "MecHostBased"` (a fixed anchor host) for these runs —
  `LocationBased` would have nothing to read.

### 5b. `NotifyOnDataChange` — reactive (event-driven MEO updates)

- **agentMode:** `EVENT_ONLY` (no data frames at all — minimum bandwidth).
- **Hooks** → `emitUserUpdate`:
  - `onUserEntry`    → `(addr, last="",  new=MEH)`
  - `onUserHandover` → `(addr, last=A,   new=B)`
  - `onUserExit`     → `(addr, last=MEH, new="")`
- **`handleDataMessage()`** → effectively unused (no data frames arrive). Its only
  body is the safety-net loop, which therefore **does not run** in this mode.
- **Why:** the MEO learns of every entry/handover/departure within ~`frameInterval`
  and reacts (instantiate near the UE / migrate / tear down). Always correct, always
  after the fact.
- **Nuance (L7):** with no data frames there's **no lost-packet safety net** —
  departure correctness rests on the EXIT frame being delivered (lossless backhaul in
  sim, so fine). Don't enable `removeInactiveUsers` here: stable users never refresh
  their `timestamp`, so it would wrongly purge present users (C1).

### 5c. `SendToExternalServer` — proactive (ML prediction via Flask)

- **agentMode:** `EVENT_AND_DATA` (the model needs the feature stream).
- **Hooks** → identical to reactive (`emitUserUpdate`). So you still get correct
  event-driven updates **and** ML predictions — the two paths compose.
- **`handleDataMessage()`** → `formatSnapshot()` builds JSON (`cellMetrics` +
  LS-only `users`), `postToFlask()` POSTs it, `parseResponse()` turns the reply into
  `MigrationPrediction`s in `migrationPredictions`. Plus the safety net.
- **Why:** Flask predicts handovers *before* they happen; the event path catches
  whatever the model misses → best latency, zero missed handovers.
- **Nuance:** if Flask is offline, `postToFlask` times out and returns empty;
  `parseResponse` no-ops. The sim keeps running on the reactive path alone.

| | SaveDataHistory | NotifyOnDataChange | SendToExternalServer |
|---|---|---|---|
| agentMode | EVENT_AND_DATA | EVENT_ONLY | EVENT_AND_DATA |
| Hooks emit MEO update? | ❌ (CSV only) | ✅ | ✅ |
| Data frame used for? | CSV features | — | Flask features |
| MEO output | none | USERS_UPDATE | USERS_UPDATE + MIGRATION_PLAN |
| Safety net active? | yes (CSV) | no | yes |

---

## 6. Cross-cutting machinery (read these once, they apply everywhere)

- **F2 exit-hold** (`UserState::pendingExitTime`). An EXIT doesn't remove a user
  immediately — it opens a ~1-`frameInterval` window. If an ENTRY elsewhere lands in
  that window it's a **handover**; if not, it's a **departure**. This is the *only*
  Controller timer and the only thing that can reconcile an EXIT-from-A and an
  ENTRY-at-B that arrive as two independent UDP packets in unknown order.
- **Timer-driven expiry** (`expireHolds` self-message, every `frameInterval`). Calls
  `expirePendingExits()` so a hold still resolves into a departure **even if no event
  frame ever arrives again** (quiet region, or EVENT_ONLY mode). `handleEventFrame`
  also calls it inline for promptness when frames *are* flowing.
- **Semantic hooks** (`onUserEntry/Handover/Exit` on `LocationDataHandlerPolicyBase`).
  The Controller *classifies* the transition; the policy *decides* what to do. This
  is why the same `handleEventFrame` serves all three modes unchanged.
- **`emitUserUpdate` vs CSV.** The single fork between "tell the MEO" and "just log."
  Reactive/proactive call `emitUserUpdate`; SaveDataHistory writes CSV. Nothing else
  differs in the event path.
- **`samplesSinceChange` / `firstDetectedAt`.** Metadata only (D1). Logged for
  analysis (confidence, detection→action latency). They do **not** gate anything.
- **`removeInactiveUsers()` (safety net).** Removes users whose `timestamp` is older
  than `threshold`. Valid **only in EVENT_AND_DATA** (data frames keep timestamps
  fresh). It's a lost-packet backstop, not the normal departure path.

---

## 7. The MEO side (what RAVENS feeds, briefly)

- **Instantiation is NOT RAVENS-driven.** A UE's `DeviceApp` → `UALCMP` →
  `CREATE_CONTEXT_APP` → MEO `startApplication()`. RAVENS' `USERS_UPDATE` drives only
  **migration** and **termination** via `reactOnUpdate()` (`MigrateOnPrediction`):
  `new==""` → remove app; `last==""` → migrate to new; both set & differ → migrate.
- **`LocationSelectionBased`** (a `selectionPolicy`) reads `userMEHMap`, which RAVENS
  populates from `USERS_UPDATE`, to instantiate near the UE's current MEH. So it only
  makes sense in reactive/proactive modes; in SaveDataHistory `userMEHMap` is empty.

---

## 8. Where to look (file map)

| Want to understand… | Open |
|---|---|
| Agent LS diff + sample counting | `RavensAgentApp/RavensAgentApp.cc` → `handleLSMessage()` |
| Agent frame sending | same → `sendEventFrame()`, `sendDataFrame()`, `handleSelfMessage()` |
| RNIS cell aggregation | same → `handleRNISMessage()` |
| Handshake (Controller side) | `RavensControllerApp/RavensControllerApp.cc` → `socketDataArrived(TcpSocket*)`, `sendInfrastructureDetailsAck()` |
| **The core transition logic** | same → `handleEventFrame()`, `expirePendingExits()` |
| Data-frame state update | same → `updateUserStateMap()`, `updateMehStateMap()` |
| MEO output | same → `handleSelfMessage("sendSnapshot")` |
| Hook interface + MEO helper | `LocationDataHandlerPolicies/LocationDataHandlerPolicyBase.{h,cc}` |
| Each strategy | `LocationDataHandlerPolicies/{SaveDataHistory,NotifyOnDataChange,SendToExternalServer}.cc` |
| User state struct | `RavensControllerApp/RavensControllerApp.h` → `struct UserState` |
