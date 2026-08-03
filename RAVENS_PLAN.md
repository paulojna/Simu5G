# RAVENS — Telemetry & MEO Round

Working plan. One step at a time; steps are added here as they are reviewed and agreed.

**Naming rule:** descriptive names only. No codenames, no letter-number labels
(`D1`, `C2`, `F2`). If a mechanism needs a name, it gets a name that says what it does —
*absence grace period*, *exit confirmation window*.

---

## Goal

Two fronts, in this order:

1. **Make the telemetry usable as model input.** Two sequence models consume it — a
   handover-timing regressor and an LSTM next-MEH classifier — and both need per-UE
   trajectories that are continuous, evenly spaced, and complete near handovers. The
   current telemetry is per-MEC-host, subsampled 3:1, and systematically truncated at
   exactly the boundaries the models care about.

2. **Open the Controller↔MEO relation.** Deliberately left unresolved until now. It
   depends on nothing in front 1 and can proceed independently (see the regeneration
   gate below).

---

## Settled decisions

Agreed through discussion. **Do not re-open these without a reason that is written down
here.** They cost a long conversation to reach.

### Architecture

**Two planes, different jobs, different guarantees.**

- **Event channel = control plane.** TCP, reliable, sparse, low-latency, always on
  regardless of mode. Carries state changes only. Cost scales with *churn*, not with
  users × sampling rate.
- **Telemetry channel = data plane for the predictor.** UDP, periodic, loss-tolerant,
  higher volume, and **on only when a consumer needs it**.

This is what makes the profiles coherent: *Reaction* = control plane only; *Prediction* =
both; *History* = telemetry as data collection.

**Presence detection stays at the Agent.** It is not moved to the Controller, even though
batched telemetry would make the information available there. Three reasons, each
independently sufficient:

- Events ride the reliable channel. If the Controller inferred presence from telemetry,
  absence-from-telemetry would become the departure signal, and a dropped UDP frame would
  be indistinguishable from a UE leaving. That failure mode currently cannot occur.
- Agent-side detection decouples control latency from telemetry cadence. Central detection
  would bound detection latency by the telemetry interval, forever, for every host.
- EVENT_ONLY mode — and therefore the whole Reaction profile and the signaling-reduction
  argument — exists *because* detection is at the edge. Centralise it and telemetry can
  never be switched off.

### Event reporting

- **Events are emitted at the Location Service tick where a state change is confirmed**, not
  at a frame boundary. This is the latency win: today a change waits up to a full telemetry
  interval on a channel that is reliable, open and idle.
- **A state change is confirmed by consecutive agreeing samples, symmetrically:**
  `entryConfirmSamples` consecutive present samples to emit ENTRY, `exitConfirmSamples`
  consecutive absent samples to emit EXIT (default 2 each, so ~1 s at the 1 s LS rate). The
  UE is retained in the Agent's local map while absent samples are being counted; a return
  resets the counter.
- **The threshold is symmetric because an asymmetric design is unsafe.** Emitting ENTRY
  immediately while holding EXIT breaks in a specific, unrecoverable way: a UE that dips into
  a neighbouring host for one sample makes that host report ENTRY, moving the Controller's
  view; the original host, having retained the UE in its map, emits nothing when the UE
  returns; the neighbour's eventual EXIT then matches the Controller's current view and the
  UE is declared gone from the system while it sits on its original host, with its app torn
  down and no event that can ever correct it.
- **Symmetric thresholds are also the only mechanism that suppresses oscillation traffic.**
  Under a hold-EXIT-only design a ping-ponging UE still costs one event per move, because the
  Controller's model is single-valued and every physical move must be reported to someone.
  With confirmation on both sides, a UE alternating at the sampling rate never accumulates
  consecutive samples anywhere and produces **no traffic at all** until it settles. A slower
  oscillation does cross the thresholds and is reported — correctly, since that UE is
  genuinely moving.
- **The Agent tracks whether the Controller has been told about each UE.** A UE that appears
  for fewer than `entryConfirmSamples` samples was never reported, so its later removal must
  be silent. This flag replaces the old frame-boundary coalescing with state that does not
  depend on when a frame happened to fire.
- **The Controller keeps a separate *exit confirmation window*.** Different question,
  different layer: the Agent asks "is this UE gone from *my* cell?" (local stability, traffic
  suppression at source); the Controller asks "did it leave the *system* or move to another
  host?" (cross-host disambiguation, which the Agent structurally cannot do). Both hosts cross
  their thresholds at roughly the same time during a handover, but the arrival order is not
  guaranteed, so the window stays. **It remains at 3 s for now** — it only needs to cover the
  skew between two hosts noticing the same handover, which is likely ~1–1.5 s, but 3 s is safe
  and over-waiting costs latency rather than correctness. Re-size from measurement later if
  exit latency turns out to matter.
- The Agent's ENTRY/EXIT **coalescing is removed**, subsumed by the thresholds. It was also
  becoming inconsistent: once telemetry carries every sample, coalescing hides an event pair
  that the telemetry still shows, so the two channels would contradict each other.
- **The Controller-side window protects *correctness*; it does nothing for *traffic*.**
  Packets are already on the wire by the time it acts. Traffic suppression can only happen at
  the source. That is why both mechanisms exist.
- **Thresholds are counted in samples, not seconds.** The LS subscription runs at a fixed 1 s
  frequency and `samplesSinceChange` already carries this unit; a time-based name would imply
  a precision the sampling does not provide.

### Telemetry

- **1 s samples batched into the periodic frame.** Reporting cadence stays decoupled from
  sensing resolution.
- **Frames are not capped or split at the application layer.** IP fragmentation handles
  oversized frames. The argument for capping was loss amplification — one lost fragment
  discards the whole datagram, so a batched frame would cost three samples instead of one —
  but that assumes a lossy path, and this one is not: mecHost → router → server runs over
  `Eth10G` datarate channels with no bit errors and no realistic prospect of queue drops at
  10 Gbps. The failure mode being guarded against cannot occur here, so the cap would be
  complexity for nothing.
- **The path MTU is 4470 B, not 1500 B.** The link uses PPP interfaces (`server.ppp++`,
  `router.pppg++`) and INET's PPP default MTU is `4470B` (`Ppp.ned:40`). `Eth10G` is only the
  channel name — a datarate channel, not an Ethernet interface — so the 1500 B Ethernet MTU
  never applies on this path. Working backwards, a frame fits without fragmenting up to
  roughly 55 samples, i.e. about **18 concurrent UEs per host** at 3 samples each.
- **INET fragments rather than drops.** `Ipv4.cc:836` passes a fitting packet straight
  through; `:847` drops only when the `DontFragment` flag is set, and that flag is set only
  when an application requests it, which UDP here does not. So an oversized telemetry frame
  is fragmented and reassembled, never silently lost.
- **The Agent warns when a frame exceeds the MTU**, so it is visible whether this ever
  actually happens across the corpus. Zero cost in the normal path. If the warning turns out
  to fire routinely, revisit capping then — with evidence.
- **Packet-count claims are measured at the IP layer**, not by counting application frames.
  That keeps the numbers honest whether or not fragmentation occurs, and removes the only
  remaining reason the application would need to care about frame size.
- **One canonical sample representation feeds both consumers.** A single producing function
  supplies the CSV columns and the prediction payload, so the two cannot drift apart. This is
  what closes the train/serve skew risk: the danger was never that the model had state, it
  was that two independently written pieces of code reconstructed the same thing differently.
- **The observing host is exact and needs no reconciliation.** A host's Location Service only
  lists UEs attached to that host's cells, so a sample appearing in host B's frame *is* an
  observation that the UE was on B at that timestamp. It is not an inference. `users.csv`
  already carries it as `MEHId`.
- **Confirmed placement is a separate, diagnostic quantity.** What the *Controller believed*
  at a given moment differs from what was *observed* for a second or two around each handover,
  because the confirming event can lag the samples. That difference is a measurement of
  control-plane lag, not an error — but it must never be used to build trajectories, which use
  the observing host.
- **The prediction server does no reconciliation.** It may buffer — a sequence model served on
  a live stream has to hold a window — but it must never work out which host a sample belongs
  to, stitch fragments across a handover, or infer movement. Those arrive already resolved.
  A stateless server was considered and rejected: it would require resending each sample once
  per interval it stays in the window (~30×), and the prediction call is blocking `curl` inside
  the simulation loop.
- **The windowing logic should be shared code, not just shared data**, between the offline
  training pipeline and the serving path — one small module imported by both. Same data
  prevents drift in the fields; same code prevents drift in how they are assembled.
- **`frameInterval` stops being one parameter doing four jobs.** It splits into
  `telemetryInterval` (data plane cadence), `absenceGracePeriod` (Agent), and
  `exitConfirmationWindow` (Controller, sized by measured cross-host detection skew rather
  than inherited from the frame cadence).

### Data files

- **Telemetry records observations; lifecycle records placement.** Telemetry observations may
  overlap — during a handover two hosts can legitimately both observe the same UE. Lifecycle
  cannot overlap; it is the Controller's reconciled model. Resolve by joining telemetry
  samples onto residency intervals derived from lifecycle events.
- **Join on `firstDetectedAt`, never on the lifecycle row timestamp.** The row timestamp is
  when the hook fired at the Controller, which lags by the reporting delay plus the
  confirmation window. `firstDetectedAt` is ground truth.
- **Key telemetry on `LocationTimestamp`, not `TimestampSent`.** The former is when the
  observation was taken; the latter is when the frame happened to ship.
- **A row is not a coherent snapshot.** Location comes from the Location Service, radio
  metrics from RNIS, on independent cycles — hence the separate `LocationTimestamp` and
  `RadioTimestamp` columns. Any model treating a row as one instant is making an assumption
  the data does not support.

### Modelling boundaries

- **The Controller and MEO are modelled as co-located management entities.** Their link is a
  direct module connection with no channel, delay, or datarate. All signaling and overhead
  results are therefore scoped to the **Agent → Controller** path, which traverses the
  modelled transport network. This is a deliberate boundary, not an omission — in a real
  deployment those two would plausibly share a server.
- **Neighbour-cell measurements are absent from the telemetry.** The actual handover is
  decided at RRC from the UE's neighbour-cell measurements; RAVENS sees serving-cell
  information only. Both models are therefore geometry-driven proxies for a decision they
  cannot observe. This is a ceiling to state in the thesis, not a bug to fix.

### Where the efficiency claim rests

**Not** on batching. Measured honestly, batching saves roughly 1.5% of telemetry bytes versus
sending at 1 s, and once frames approach the MTU its packet-count advantage disappears
entirely. Batching is justified by *coherence and resolution*, not efficiency.

The efficiency argument rests on two comparisons, both robust:

1. **Event-driven reporting vs periodic full-state polling** — the structural design
   contribution, and a large effect. Event frames are tiny and never fragment.
2. **Reaction profile vs Prediction profile** — two of our own configurations measured
   identically, so it is a ratio rather than an absolute and is immune to the byte-size
   constants being approximate. It answers "what does proactive migration *cost* in
   signaling?", which is the more interesting question.

### Working method

- **Generate at the finest resolution once; derive coarser variants offline by subsampling.**
  A single generation pass at 1 s yields the 1 s dataset *and* the 3 s dataset and anything
  between. The resolution question is then answerable with two training runs and zero extra
  simulation.
- **Log generously, prune offline.** A regeneration costs 120 runs × 60 min. A missing column
  costs another one; a redundant column costs disk.

---

## The regeneration gate

The training corpus is **120 runs × 60 min**, generated with the **History** profile.

The corpus is generated only once the implementation is complete — **including the
Controller↔MEO protocol**, even though the History profile is MEO-silent and would not be
affected by it. The reason is reproducibility: the code that produced the training data
should be the same code that runs the experiments, so there is one final state rather than a
corpus generated from an intermediate one.

- **Above the gate:** everything. Agent, Controller, and the Controller↔MEO protocol.
- **The gate:** regenerate the corpus.
- **Below the gate:** only work that cannot affect a simulation run — the prediction service's
  buffering and windowing, the offline analysis pipeline, and the model work itself.

---

## Steps

- [ ] **Step 1** — Compute real chunk lengths on the Agent↔Controller path
- [ ] **Step 2** — Split `frameInterval` into its separate jobs
- [ ] **Step 3** — Rebuild Agent event detection on stability thresholds
- [ ] **Step 4** — Batch samples into the telemetry frame
- [ ] **Step 5** — One canonical sample representation for both consumers
- [ ] **MEO round** — not yet broken into steps; see below. Must land before the gate.
- [ ] **Regenerate the corpus** — 120 runs × 60 min, History profile

---

## MEO round — identified, not yet planned

To be planned as steps once 1–5 are implemented. It must still land **before** the
regeneration, per the gate above.

### Open decision: what `USERS_UPDATE` carries

The Controller currently accumulates *transitions* in a map keyed by UE address and drains it
every snapshot. Two transitions for the same UE inside one window overwrite each other: a
UE going A→B and then B→C is reported only as `{from: B, to: C}`, and the MEO is never told
about A→B.

This is benign today but only by luck. `MecAppMigrationManager::migrateApp` finds the app by
UE address in its own registry and uses `oldMEHId` only for log lines and the result struct,
so the wrong value is ignored. The field is still wrong and the logs still are.

**Option A — keep transitions, fix the composition.** Merge rather than overwrite, so `{A→B}`
then `{B→C}` becomes `{A→C}`, and a UE ending where it started drops out. Correct, small, but
it is an invariant that every future edit has to respect.

**Option C — send placements.** The Controller sends the current placement of every UE it
knows about (`{address, currentMEH}`) every snapshot; the MEO diffs at ingress against its own
map and synthesises entry / handover / exit. The strategies keep receiving the same
`UserMEHUpdate` they receive today — it is just derived at the MEO's front door instead of
accumulated at the Controller's back door.

**Recommendation: Option C.** A placement is idempotent, so last-write-wins becomes the
correct behaviour rather than a bug — the problem stops being expressible instead of being
fixed. Three further reasons:

- **It is free here.** Full state every snapshot would be real traffic on a real link, but the
  Controller↔MEO link is deliberately unmodelled because the two are co-located. No cost in
  the model, and defensible in the write-up.
- **The MEO becomes self-correcting.** Its view currently can drift from the Controller's and
  never recover. Periodic full placements re-converge every snapshot, which also handles a
  false-positive prediction — a migration fired for a UE that did not move is corrected at the
  next snapshot, with no separate mechanism needed.
- **It mirrors a pattern already in the system.** The Location Service sends the Agent a full
  list every second and absence means gone. The same authoritative-full-state semantics at the
  Controller↔MEO boundary means one idea applied at both boundaries rather than two
  conventions.

Cost: a diff loop at MEO ingress, roughly 40 lines. `MeoOutput` loses its transition tracking
and `emitUserUpdate` entirely and becomes the placement publisher; its presence in the output
list still marks "this profile talks to the MEO".

### Known items

1. **`USERS_UPDATE` semantics** — the decision above, plus removal of the coalescing.
2. **Predicted EXITs are executed blindly.** `MigrateOnPrediction::handleScheduledEvent` calls
   `removeAppFromSystem` on a predicted exit with no reactive confirmation. A false exit
   deletes an app that is never recreated, because app creation is driven by the UE requesting
   one and the UE never re-requests. **Exits must always be reactive.**
3. **MEO execution log** — extend the migration log (executed / stale / queued / failed /
   ignored, plus reactive fallbacks) and write it into the Controller's `run_<N>` directory so
   it joins against the lifecycle CSV.
4. **Duplicated type constants** — `USERS_UPDATE` and `MIGRATION_PLAN` are `#define`d
   separately in `RavensControllerApp.cc` and `MecOrchestrator.cc`, with a `TODO` already on
   the second copy. Move to a shared header.
5. **Dead return path** — the Controller declares an `inGate` and the MEO a
   `toRavensController` gate, but `handleMessageWhenUp` has no branch for it, so anything sent
   back would fall through to the unknown-message case and be dropped. Either wire it or
   remove it; it must not stay as a gate that silently discards.
6. **Prediction sub-profiles** — trust-the-prediction versus correct-with-reactive-fallback, as
   a strategy flag on the MEO side. Controller output is identical in both.

---

### Step 1 — Compute real chunk lengths on the Agent↔Controller path

**Why:** Every RAVENS packet declares a hardcoded size, so packet size is fiction — a frame
carrying 2 users and one carrying 40 both claim 500 bytes. Control-plane overhead, the
Reaction-vs-Prediction signaling comparison, and any statement about what batching costs are
all unmeasurable until this is fixed. It is a prerequisite for the signaling results, not an
optimisation.

**Change:** Frame size reflects content. A telemetry frame carrying 20 users is larger than
one carrying 2; an event frame with 5 events is larger than one with 1. Each send site calls
a sizing function that takes the payload it is about to send, and those functions live in one
shared header alongside named per-record constants.

**Files:**
- New `src/apps/mec/RavensApps/RavensLinkSizes.h` — the constants and the sizing functions,
  alongside `RavensLinkProtocol.h` and shared by both ends for the same reason
- `src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.cc` — `sendJoinNetworkRequest`,
  `sendAPList`, `sendEventFrame`, `sendDataFrame`
- `src/apps/mec/RavensApps/RavensControllerApp/RavensControllerApp.cc` —
  `sendJoinNetworkAck`, `sendInfrastructureDetailsAck`

**Sizing constants** (fixed binary encoding; each needs a comment naming the fields it covers):

| Constant | Bytes | Basis |
|---|---|---|
| `FRAME_HEADER_B` | 24 | type 4 + requestId 4 + timestamp 8 + host id 8 — every Agent→Controller frame |
| `ACK_HEADER_B` | 16 | type 4 + requestId 4 + timestamp 8 — Controller→Agent replies carry no host id |
| `CONFIG_ACK_PAYLOAD_B` | 12 | infoType + telemetry interval + agent mode |
| `USER_RECORD_B` | 80 | location 40: address 4, cell id 4, x/y/z 12, bearing 2, speed 2, distance 4, timestamp 8 · radio 40: 2 delays 8, 2 PDRs 8, 2 volumes 8, RSRP 2, timestamp 8 |
| `CELL_RADIO_RECORD_B` | 64 | cell id 4 + timestamp 8 + 10 aggregate metrics — **one per telemetry frame**, not one per user |
| `AP_RECORD_B` | 16 | AP id 4 + x/y/z 12 — **n per infrastructure-details frame** |
| `EVENT_RECORD_B` | 16 | address 4 + event type 1 + sample count 1 + firstDetectedAt 8 |

Frame length = `FRAME_HEADER_B + n × RECORD_B`, plus `CELL_RADIO_RECORD_B` once on a
telemetry frame that carries cell aggregates.

**Sizing is computed per message, not hardcoded per call site.** `RavensLinkSizes.h` exposes
`telemetryFrameBytes(users, hasCellRadio)`, `eventFrameBytes(events)` and
`infrastructureFrameBytes(aps)`, each taking the payload actually being sent. This does not
change the number — for a fixed-width encoding `header + n × record` is the closed form of
walking the fields — but it means a field added to `UserData` or `RavensEvent` is a one-place
update, and the call site reads as "size this message" rather than asserting a constant.

**Why not measure the real serialized size.** Three reasons, checked against the code:
`FieldsChunk::getChunkLength()` returns only what was set and no `ChunkSerializer` is
registered for the RavensLink messages, so there is nothing to read off; IP-layer byte counts
are *derived from* the chunk length we set, so the constants define the measurement rather
than approximate it; and every radio field is always present (a missing RNIS value is written
as the `-1` sentinel, never omitted), so the only content that genuinely varies in length is
the `std::string` address and cell id — whose length is an OMNeT++ module-naming artifact,
not a protocol property. A real encoding puts an IPv4 address in 4 bytes flat.

Writing real serializers (`Register_Serializer`, ~200 lines) is the correct route only if
packets ever need to be byte-exact on a real wire — emulation, pcap export, comparison with a
real deployment. None of that is in scope, and the Reaction-vs-Prediction ratio is already
immune to the constants being approximate.

**Done when:**
- No `setChunkLength` on the Agent↔Controller path uses a literal
- Every Agent↔Controller send site sizes its frame by calling a function in
  `RavensLinkSizes.h` with the payload it is sending
- Telemetry frame size scales with user count; event frame with event count; infrastructure
  details with AP count
- A short run shows frame sizes differing across frames
- Each constant carries a comment stating which fields it accounts for
- The two Controller→MEO `setChunkLength` calls (`RavensControllerApp.cc`, in
  `handleSelfMessage`) carry a comment noting the link is unmodelled and the value therefore
  arbitrary — so the next reader does not mistake it for a modelled quantity
- **Measured and recorded:** peak and typical concurrent UEs per MEC host, and the resulting
  telemetry frame size, both as-is and projected under 3-sample batching. The question this
  answers is whether busy hosts exceed **~18 concurrent UEs**, above which a batched frame
  passes the 4470 B path MTU and fragments. Estimate to check against: ~10–40 concurrent UEs
  per host (4900 vehicles over 3600 s across 11 hosts), so ~0.8–2.4 kB unbatched and
  ~2.2–6.6 kB batched. Fragmentation is acceptable (see settled decisions) — this measurement
  establishes how often it happens, not whether to prevent it.
  **This needs no new code and no new run:** `run_<N>_users.csv` already carries
  `TimestampSent` and `MEHId`, so concurrent UEs per host is `group by (TimestampSent, MEHId)`
  on any existing run, and frame size follows from the constants above.

**Depends on:** —

**Not this step:** changing what frames contain; modelling the Controller↔MEO link;
implementing the MTU cap (that is a telemetry-step requirement, already decided — this step
only measures the input to it).

---

### Step 2 — Split `frameInterval` into its separate jobs

**Why:** One parameter currently sets the telemetry cadence, the event cadence, the
Controller's exit hold length, and the hold-sweep period. These are unrelated quantities
that merely happen to share a value: the telemetry cadence is a data-volume choice, while
the exit hold is sized by cross-host detection skew. Both the event step and the telemetry
step need to move them independently, and neither can while they are the same number.

**Change:** Two named parameters replace one. `telemetryInterval` (Controller-owned, pushed
to Agents in the handshake ACK) drives the Agent's frame timer. `exitConfirmationWindow`
(Controller-private, never transmitted) sets the exit hold, and the periodic sweep derives
from it at half its length rather than equalling it. Defaults otherwise reproduce today's
timing.

**Files:**
- `RavensControllerApp.ned` / `.h` / `.cc` — replace `frameInterval` with
  `telemetryInterval` and `exitConfirmationWindow`; sweep schedule (`initialize`,
  `handleSelfMessage`); ACK payload (`sendInfrastructureDetailsAck`); `pendingExitTime`
  computation (`handleEventFrame`)
- `RavensAgentApp.ned` / `.h` / `.cc` — rename to `telemetryInterval`, documented as the
  pre-negotiation default that the ACK always overrides; frame timer; ACK handler
- `RavensLinkPacket.msg` — rename ACK field `rate` → `telemetryIntervalMs` (*rate* and
  *interval* are inverses, so the current name says the opposite of what it holds).
  Regenerates `RavensLinkPacket_m.*`
- `simulations/NR/mec/ravensEnabledScenarios/tust_1to1/omnetpp.ini` — update the parameter
  name, add `exitConfirmationWindow`, fix the `staleWarningThreshold` comment that
  references the old name

**Done when:**
- No `frameInterval` identifier remains anywhere in the RAVENS path, including comments
- The Controller pushes only the telemetry interval to Agents; `exitConfirmationWindow` is
  never transmitted and the Agent has no knowledge of it
- The sweep period derives from `exitConfirmationWindow` rather than equalling it, so the
  effective window is bounded at [window, 1.5×window] instead of [window, 2×window]
- A run reproduces prior lifecycle output apart from exits confirming sooner in quiet
  periods — that difference is expected and is the one intended behaviour change
- No comment anywhere still describes a single interval serving several purposes

**Depends on:** —

**Not this step:** introducing `absenceGracePeriod` — that parameter arrives with the
mechanism that uses it. Moving event frames off the telemetry timer. Changing any default.

**Known temporary awkwardness:** event frames still ride a timer now named
`telemetryInterval`. The event step removes them from it. Better than inventing a third
name we would delete two steps later.

**Note on the Agent's parameter:** the Agent's own value is dead today — it is set at init
and always overwritten by the ACK, and no frame is sent before the ACK arrives (the frame
timer starts only after the LS subscription confirms, which is downstream of the
handshake). It is kept, renamed, and documented as a pre-negotiation default rather than
deleted, so the member is never uninitialised.

---

### Step 3 — Rebuild Agent event detection on stability thresholds

**Why:** Events are emitted only at the frame tick, adding up to a full telemetry interval
of delay to every state change, on a channel that is reliable, open and idle. Emitting on
detection removes that delay — but doing so naively makes a UE that briefly touches a
neighbouring host trigger a handover, and combined with any retention of the UE in the
Agent's map it can leave the Controller believing a UE has left the system while it sits on
its original host, unrecoverably. Confirming state over consecutive Location Service samples
fixes both: transient visits are never reported, and a UE oscillating at the sampling rate
produces no traffic at all.

**Change:** The Agent tracks, per UE, consecutive present and absent sample counts plus
whether the Controller has been told about it. A state change is emitted at the LS tick
where its threshold is crossed, with all UEs crossing in the same tick batched into one
frame. The frame-boundary pending maps and their coalescing rules are removed.

**Files:**
- `RavensAgentApp.h` — replace `PendingEvent`, `pendingEntries_`, `pendingExits_` with
  per-UE event state: consecutive present, consecutive absent, reported flag,
  firstDetectedAt
- `RavensAgentApp.cc` — `handleLSMessage` (the diff logic), `sendEventFrame` (invoked on
  threshold crossing rather than from the timer), the `sendUserList` timer handler
- `RavensAgentApp.ned` — `entryConfirmSamples`, `exitConfirmSamples`, both default 2

**Done when:**
- Event frames are emitted from the LS notification path, not the frame timer
- A UE present for fewer than `entryConfirmSamples` consecutive samples is never reported,
  and its later removal emits nothing
- A UE alternating between two hosts at the LS sampling rate produces zero event frames
- Multiple UEs crossing a threshold in the same LS tick produce one frame, not several
- The old coalescing branches are gone and no event logic depends on frame boundaries
- `samplesSinceChange` still carries the confirmed count and stays consistent with the
  threshold that triggered the event
- Lifecycle output over a full run shows the same transitions as before, shifted earlier,
  minus transient visits that were previously reported

**Depends on:** Step 2

**Not this step:** telemetry batching; anything Controller-side. The Controller's exit
confirmation window and its stale-EXIT guard already handle cross-host ordering and need no
change.

---

### Step 4 — Batch samples into the telemetry frame

**Why:** The Agent overwrites a UE's position every second and sends only whatever value
happens to be current when the frame fires, so two of every three observations are thrown
away — at 50 km/h that leaves 42 m between consecutive points in a trajectory, and ~90 m on
the motorway scenario. The loss is also not evenly spread: a departing UE is removed from the
map immediately, before the next frame, so its final observations are never transmitted at
all. That is precisely the approach-to-boundary segment both handover models need.

**Change:** The Agent keeps a per-UE buffer of samples taken at each Location Service tick
and sends everything accumulated since the last frame. A sample is a snapshot of one tick —
the position from that tick plus the radio values as they stood, each with its own timestamp,
so a repeated radio value is identifiable. Samples buffered for a UE that has since departed
are still sent. Frame cadence is unchanged: same number of frames, more inside each.

**Files:**
- `RavensLinkPacket.msg` — `RavensLinkDataFrameMessage` carries a flat list of samples
  instead of a map of current values (regenerates `RavensLinkPacket_m.*`)
- `RavensAgentApp.h` / `.cc` — per-UE sample buffer alongside the existing `users` current
  state; `handleLSMessage` appends a sample per observed UE per tick; `sendDataFrame` sends
  the buffer and clears it, and warns if the frame exceeds the path MTU
- `RavensControllerApp.cc` — `updateUserStateMap` takes the newest sample per UE for the
  world model
- `Outputs/HistoryOutput.cc` — one CSV row per sample instead of per UE
- `Outputs/PredictionOutput.cc` — iterate samples rather than the current-value map

**Done when:**
- A telemetry frame carries every Location Service observation since the previous frame, not
  just the latest
- A departing UE's buffered samples are delivered rather than discarded with its map entry —
  verified by confirming the last sample before a departure now reaches the CSV
- `users.csv` gains rows but keeps the same columns, with sample timestamps falling between
  the old ones
- An oversized frame is confirmed to fragment and arrive intact, not to be dropped — checked
  once, deliberately, since silent loss on busy hosts would otherwise go unnoticed
- The Agent logs a warning when a frame exceeds the MTU, and it is recorded how often this
  fires across a representative run

**Depends on:** Steps 1, 3

**Not this step:** assembling per-UE trajectories in the Controller and making the prediction
server stateless — that is step 5. Here the Controller and both outputs change only enough to
keep working with the new frame shape. No application-level frame cap or splitting.

**Note on unreported UEs:** a UE seen for fewer than `entryConfirmSamples` ticks is never
announced as an event, but it is in the Agent's map and its samples are buffered, so it
appears in the telemetry and in `users.csv`. This is intended — telemetry records
observations, lifecycle records placement — and it keeps transient visits available offline
even though the event stream deliberately suppresses them.

**Note on pruning:** `users.csv` keeps one row per sample. A clean training set is produced
offline by filtering to samples whose timestamp falls inside a confirmed residency from
`lifecycle.csv`. Same principle as the predictions log: an extra row costs disk, a missing
one costs a regeneration.

---

### Step 5 — One canonical sample representation for both consumers

**Why:** The CSV columns and the prediction payload are built by separate code that happens
to agree today. Nothing enforces that, and nothing would report it if they diverged — a field
added, renamed, or computed differently on one side would produce a model trained on one
distribution and served on another, silently. Making both come from one producer removes the
possibility rather than relying on discipline.

**Change:** A single function produces a sample record. `HistoryOutput` writes it as CSV
columns; `PredictionOutput` serialises the identical fields. A `confirmedMEH` field is added
alongside the existing observing `MEHId`, recording what the Controller believed at that
moment — diagnostic only, and cheap to add now given what a regeneration costs.

**Files:**
- `Outputs/RavensOutputBase.h` / `.cc` — the shared sample record and its producer; add a
  per-UE sample hook alongside the frame-level `onTelemetry`, which stays for the cell-level
  radio aggregates
- `RavensControllerApp.cc` — dispatch samples to the per-UE hook, tagging each with the
  confirmed placement from `userStateMap`
- `Outputs/HistoryOutput.cc` — user rows from the sample hook, cell rows from the frame hook
- `Outputs/PredictionOutput.cc` — payload built from the shared record

**Done when:**
- Every field in a `users.csv` row and in the prediction payload comes from the same producing
  function — adding a field to one adds it to the other by construction
- `MEHId` (observed) and `confirmedMEH` (believed) are both present and differ only around
  handovers; the gap between them is measurable as control-plane lag
- Cell-level radio aggregates still come from the frame-level hook, unchanged
- No Controller-side buffering or reordering is introduced — samples are emitted as they
  arrive and sorted by whoever consumes them

**Depends on:** Step 4

**Not this step:** the prediction service itself. Its buffering and windowing are deferred
until after the regeneration — it does not affect the History profile and therefore does not
block the corpus. **Carry forward:** when that work happens, the windowing must mirror the
offline pipeline exactly, ideally by importing the same module rather than reimplementing it.
