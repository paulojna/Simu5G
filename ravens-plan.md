# RAVENS — Telemetry & MEO Round

Working plan. Steps 1–6 are implemented; what remains before the regeneration gate is the
MEO round, planned separately in [meo-plan.md](meo-plan.md).

**Naming rule:** descriptive names only. No codenames, no letter-number labels
(`D1`, `C2`, `F2`). If a mechanism needs a name, it gets a name that says what it does —
*absence grace period*, *exit confirmation window*.

---

## Goal

Two fronts, in this order:

1. **Make the telemetry usable as model input.** Two sequence models consume it — a
   handover-timing regressor and an LSTM next-MEH classifier — and both need per-UE
   trajectories that are continuous, evenly spaced, and complete near handovers. *(Done —
   Steps 1–6.)*

2. **Open the Controller↔MEO relation.** Deliberately left unresolved until now. It
   depends on nothing in front 1. *(Outstanding — see [meo-plan.md](meo-plan.md).)*

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
  at a frame boundary. This is the latency win: previously a change waited up to a full
  telemetry interval on a channel that is reliable, open and idle.
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
- **Lifecycle may build labels. It may not filter features.** A label is a fact about what
  happened *after* a sample, and the prediction server never reproduces one — it predicts it.
  A feature filter is different: the server receives telemetry and nothing else, so a training
  set pruned by a lifecycle join carries a preprocessing step the serving path cannot repeat.
  That is train/serve skew one level above the one Step 5 removes.
- **The feature filter is the per-row test `confirmedMEH == MEHId`.** Both fields travel in the
  telemetry sample, so the offline pipeline and the prediction server apply the identical test
  — no join, no reconstruction, nothing to keep in step by hand. It is deliberately stricter
  than a lifecycle join on `firstDetectedAt`: it drops the first one or two samples of every
  residency, the interval before ENTRY was confirmed. Those are seconds in which the system did
  not yet know where the UE was, and the server will not know either, so excluding them from
  training matches what is served. Training on the unfiltered stream was the alternative — no
  filter code to keep in sync at all — and is the fallback if the strict filter costs too much
  data near boundaries, which is exactly where the models need it. Decide from the trained
  model, not in advance: `users.csv` keeps every row either way, so this is a pipeline choice
  below the regeneration gate.
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

## Implementation record — Steps 1–6

All six are implemented and committed. Kept as a one-line record; the reasoning that produced
them is in *Settled decisions* above, which is the part worth re-reading.

| Step | What it did | Commit |
|---|---|---|
| 1 | Compute real chunk lengths on the Agent↔Controller path — frame size reflects content, sizing functions in `RavensLinkSizes.h` | `48b474ad` |
| 2 | Split `frameInterval` into `telemetryInterval` and `exitConfirmationWindow` | `48b474ad` |
| 3 | Rebuild Agent event detection on stability thresholds; events emitted at the LS tick, not the frame boundary | `5107b80b` |
| 4 | Batch samples into the telemetry frame — every LS observation is sent, not one in three | `aa253997` |
| 5 | One canonical sample representation for both consumers; `removeInactiveUsers` becomes the diagnostic `reportSilentUsers` | `c7c2349e` |
| 6 | One cell record per RNIS notification; the long-lived mutable `AccessPointRadioInfoData` is gone | `9c99ed79` |

Two fixes landed alongside them and are worth knowing about, because both were latent for a
long time and one was load-bearing for a later crash:

- `c9bbfcfd` — `socketDeleted()` was erasing from `socketMap` while `deleteSockets()` was
  iterating it.
- `aa253997` — **EXIT detection had never fired.** The departure loop removed a UE from the
  `users` map on its *first* absent sample, and only walked that map, so the absent counter
  could never reach `exitConfirmSamples` (default 2). No Agent had ever emitted an EXIT.
  Fixing it exposed a long-standing MEC app teardown crash; see [inet-fix.md](inet-fix.md).

---

## Remaining before the gate

### MEO round

Planned separately in **[meo-plan.md](meo-plan.md)**. Must land before the regeneration.

### Agent does RNIS work it never uses in EVENT_ONLY mode

Small, fully specified, independent of the MEO round.

**Problem.** In `EVENT_ONLY_MODE` the Agent sends no telemetry frames, but
`RavensAgentApp::handleRNISMessage` still runs in full on every notification (once a second,
per host): it parses the notification JSON, builds a cell record that is then discarded, and
writes per-UE radio values into the `users` map that are only ever read when a sample is
copied for a telemetry frame — which that mode never sends. All of it is dead work, and the
JSON parse is the expensive part on a busy cell.

**Fix.** At the top of the `code == 200` branch in
`src/apps/mec/RavensApps/RavensAgentApp/RavensAgentApp.cc`:

```cpp
if (code == 200)
{
    // Nothing consumes RNIS data in this mode: the cell record would be built
    // and dropped, and the per-UE values it attaches to the users map are only
    // read when a sample is copied for a telemetry frame, which this mode never
    // sends. Returning here also skips the JSON parse, which is the part that
    // costs something on a busy cell.
    if (agentMode_ != FULL_MODE)
        return;
    ...
```

`agentMode_` starts at `FULL_MODE` and is corrected by the handshake ACK, so a few
notifications early in the run still do the work. Harmless.

**Deliberately not part of this.** The L2Meas subscription itself is created regardless of
mode, so the RNIS builds and sends a notification every second for a profile that reads none
of it. Skipping the subscription entirely means either deferring it until after the handshake
or unsubscribing once the mode is known — `sendL2MeasSub` is scheduled from the RNIS socket's
`established()`, while the mode arrives with `INFRASTRUCTURE_DETAILS_ACK`, and there is no
guarantee of ordering between them. That is a sequencing change with real risk; treat it as
its own task if wanted.

This does not affect any published signaling number — those are scoped to the
Agent → Controller path, and this traffic is internal to the MEC host. It does mean the claim
that event-only mode does "no telemetry work" is currently looser than it reads.

---

## Related documents

- **[meo-plan.md](meo-plan.md)** — the MEO round, the last work above the gate.
- **[inet-fix.md](inet-fix.md)** — a local, unversioned modification to INET's
  `MessageDispatcher` that the MEC app teardown path depends on. Reapply it after any INET
  reinstall, or the long runs start crashing again.
