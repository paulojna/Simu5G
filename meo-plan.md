# Orchestrator internals

Refactor 2 of three. Refactor 1 (the Controller ↔ orchestrator boundary) is done; see
[meo-controller-plan.md](meo-controller-plan.md) for what it settled and why. Refactor 3
(Controller ↔ external model server) is untouched and appears here only where it constrains
this one.

Naming rule: descriptive names only. No letter labels.

---

## Scope

How the orchestrator decides things, and what it remembers in order to decide them. The
message shapes arriving at its door are fixed and not reopened here.

---

## What is already settled

Recorded so it is not re-litigated. All of this landed with refactor 1.

- **Three streams arrive, independently switchable.** Confirmed user events (immediate),
  predictions (immediate), telemetry (one window per `telemetryInterval`). Types and event
  kinds live in `RavensControlProtocol.h`, shared by both ends.
- **Events say what happened.** `UserEvent` carries `eventType` — entry, handover, exit —
  rather than leaving the receiver to infer it from which host name is empty. Every strategy
  now switches on it, and the "unexpected update" branch each of them used to carry is gone.
- **`observedAt` is carried end to end**, so staleness is measured rather than inferred from
  the configured intervals.
- **Predictions carry `expectedAt` as an absolute time**, plus `confidence` and `modelId`.
  Nothing may branch on `modelId`.
- **No placement snapshot.** Decided deliberately: the orchestrator's own UE view is fed by
  events, and enriched by telemetry where telemetry is on. A third source restating the same
  thing is one more thing that can disagree.
- **Dead ends removed:** the duplicated type `#define`s, the return gate that silently
  dropped anything sent back, the undefined `getMecHostIdFromAccessPointId`, and
  `MigrateAppMessage`'s bare `0`/`1`, now `MIGRATE_APP_MOVE` / `MIGRATE_APP_EXIT`.

Still outstanding from that round, carried here because both are orchestrator-adjacent:

- **`ObserveOnly` does not exist.** A strategy that hears everything and acts on nothing.
  Needed by two runs: collecting a training set, where applications should not be shuffled
  underneath the users being recorded; and measuring a model, where predictions should be
  produced and logged without paying for a single migration. Until it exists, the collection
  profile runs the default `MigrateOnPrediction` and migrates while it records.
- **`ensureRunDirectory` is not recursive.** `mkdir` is called once, so a `path` whose parent
  is absent fails with `ENOENT`, every `ofstream` then fails quietly, and the run produces no
  CSV files at all while otherwise appearing to succeed. Controller-side and small; it blocks
  trusting any output, so it goes first.

---

## What the orchestrator remembers

The central piece of this round, and the thing every item below depends on.

**Two structures, not one.** A view of the *users* and a view of the *applications*. They are
not the same map and must not be conflated: a proactive migration moves an application before
its user moves, so for the duration of that gap the two legitimately disagree, and that
disagreement is the mechanism working rather than an error to be smoothed away.

Today there is one `userMEHMap`, and the orchestrator mutates it itself — `removeAppFromSystem`
erases from it — which means it is already half an application map wearing a user map's name.

- **User view.** Fed by events, which are authoritative and arrive in every profile. Enriched
  by telemetry when telemetry is on: `observedMEH` on a sample is exact, not inferred, since a
  host's Location Service only lists users on that host's own cells. Events remain the source
  of truth; telemetry adds detail between them, never contradicts them.
- **Application view.** Where each application actually is, and what state it is in — placed,
  migrating, awaiting confirmation, gone. Owned by the orchestrator alone. RAVENS has no
  opinion about it and never sees it.

Deciding what each holds, and which one every existing call site meant, is the first task of
this round.

---

## Known items

### 1. Wire the decision log

`DecisionLogger` and `OrchestrationDecision` are written, compiled, and referenced by nothing
outside their own directory. The record already speaks the right vocabulary — `trigger`,
`observedAt`, `expectedAt`, `modelId`, `kind`, `outcome`, joined on `requestNumber` — and
refactor 1 made every one of those fields available at the boundary.

Two things this replaces:

- `MigrateOnPrediction` writes its own ad-hoc `migration_log_run<N>.csv` into the working
  directory. It should write decisions instead, into the run directory, so it joins against
  `lifecycle.csv` on user and time.
- `latePredictions_` is currently a counter printed at destruction. It is the direct
  measurement of whether the model predicts far enough ahead — a prediction that arrived with
  less lead time than a migration takes — and belongs in the log as a per-decision fact, plus
  a scalar.

Worth logging now that both became observable: whether a migration's old instance was actually
destroyed, and whether a delete was executed against a user the orchestrator still believed to
be placed.

### 2. Exit removes, entry places

**Decided: a confirmed exit deletes the application.** This item spent two rounds looking for a
way to avoid that, and the search was misconceived.

The history is worth keeping, because the reasoning failed twice in different ways. It first
read *"verify the user is absent from the placement view before deleting"* — a guard that would
not have caught the bug it was written for, since on a confirmed exit the placement view
*agrees* the user is gone, so the check passes and the delete proceeds. It then read *"teardown
must tolerate the user still being present"*, treating deletion-under-a-live-user as the defect.

That is the part that was wrong. What "gone" means to RAVENS is **gone from its observation**,
not gone from the simulation — the module can still exist, still be attached, and still be
sending every 100 ms, since `UEPerfApp::sendRequest` reschedules unconditionally until
`stopTime`. But the orchestrator declining to act on that would be it second-guessing its only
source of truth, and the cost of acting on a wrong observation is precisely what separates the
reactive, proactive and learning modes. Guard it and all three look equally good at the thing
being measured.

**The cost is already instrumented.** `UEPerfApp` arms a timeout per request and emits
`lostMessages` when one expires, declared in its NED as
`@statistic[lostMessages](record=stats, mean, vector)`; `responseTime` covers the answered side.
A user transmitting into a void is a recorded per-user number today, not merely a visible
symptom. It follows that **the user not being told is also correct** and should stay: an edge
application vanishing does not notify its client in reality either, and the client timing out is
what produces the measurement.

**What actually remains is irreversibility, which is a different problem.** Nothing ever
recreates the application: creation is driven by the user requesting one, the user never
re-requests, and on entry `checkIfMigrationIsNeeded` finds no application and reports that no
migration is needed. So a false exit at t=50 of a 300-second run is not the bounded cost of one
wrong decision — it is one user dark for 250 seconds, whose dead weight swamps the very metric
it was supposed to inform. An absorbing state cannot serve as a cost signal.

**The work, then, is symmetry rather than a guard: entry places, exit removes.** A user that
reappears and has no application gets one back. The wrong decision still costs — the user stays
dark until RAVENS re-observes it, and those lost packets are still recorded — but the cost is
bounded and comparable across modes.

One constraint on the shape: the orchestrator must not place an application for a user that
never asked for one, so restoration has to be conditional on the user having had one before.
That is a fact about applications, not users, so it comes out of the application view rather
than being separate machinery — see § *What the orchestrator remembers*.

**Deletion is now safe at the transport layer**, which it was not before. That removed the
crash; it is unrelated to any of the above.

### 3. Migration failure has no path

`MigrateOnChange` carries a literal `// Fallback strategy here?`. Nothing retries, nothing
records the failure as anything but a log line, and the user keeps using the old endpoint.
Refactor 1 gave this boundary the information needed to notice; this round decides what to do
with it. Depends on the application view (§ *What the orchestrator remembers*) existing first.

### 4. Consuming telemetry

`reactOnTelemetry` is declared with a default no-op body and no implementation. It receives
`windowStart` / `windowEnd`, the list of hosts that actually reported, and the user and cell
samples.

Note the shape before building on it: a window holds **one row per (user, observing host)**,
which is not one row per user. A user crossing between hosts mid-window is seen by both and
appears twice with different `observedMEH` — the most informative rows in the set. A lost frame
means zero rows, which is why the reporting-host list is carried rather than inferred.

### 5. Prediction sub-profiles

Trust-the-prediction versus correct-with-reactive-fallback, as a flag on the strategy.
Controller output is identical in both, which is what makes them comparable. Today
`MigrateOnPrediction` always does both and deliberately does not let a handover cancel a
pending prediction, so the reactive path is measurable against the proactive one rather than
quietly substituting for it. Making that a switch is the work.

### 6. Direction of configuration authority

Independent axes made every sensible combination expressible, which was the point. What they
do not do is stop a *nonsensical* combination: `telemetry = false` with
`reactionStrategy = "MigrateOnPrediction"` is a proactive run that receives no predictions and
silently degrades to reactive. The Controller cannot catch it — the strategy is here. This
module cannot catch it — the switch is there.

**Proposed shape: the orchestrator is the authority and configuration propagates downward.**
It is told what it consumes; the Controller derives what it must collect; the Agents derive
what they must send. The chain already runs in that direction — the Controller pushes
`telemetryInterval` and `agentMode` to the Agents in `INFRASTRUCTURE_DETAILS_ACK` — so this
extends an existing mechanism by one hop rather than inventing one. Invalid combinations become
unrepresentable instead of merely detected, and the init-time check that currently throws stops
being needed.

**The trap, which is the whole reason the single `mode` string was split up:** a profile must
set *defaults*, not decide. The moment it decides, `SaveDataHistory` / `NotifyOnDataChange` /
`SendToExternalServer` have been rebuilt one level up and the same combinations become
inexpressible again. Concretely, the learning ablation is *predictions on versus off, with
everything else identical*; if a profile name fixes the prediction stream, that ablation
becomes a code change instead of a configuration change.

So: profile expands to defaults, every axis stays individually overridable underneath.

---

## Risk carried forward

**The INET modification is unversioned.** Deleting a MEC application used to crash the
simulation through an unbounded recursion between INET's `MessageDispatcher` and a disconnected
gate. Closed by releasing sockets before deleting the module, plus a local INET change
documented in [inet-fix.md](inet-fix.md). **That change will be lost on any INET reinstall**,
and this round is the one that makes deletion happen often.

**The trigger for revisiting the deferred drain window:** every `MessageDispatcher` records a
`discardedToDisconnectedGate` scalar. If the discards are teardown confirmations (`CLOSED`,
`closed`, `PEER_CLOSED`), nothing further is needed. If application data
(`RequestResponseAppPacket`) appears, applications are being deleted under live users.

Note that this is no longer an input to item 2, which is decided: applications *are* deleted
under live users, deliberately, and the resulting lost packets are a measurement. The scalar
now answers a narrower question — whether those packets are being discarded somewhere that
loses them before `UEPerfApp`'s timeout can count them. If so, the metric under-reports and the
drain window matters again.

---

## Suggested order

1. `ensureRunDirectory`, then `ObserveOnly` — nothing can be trusted or measured before both.
2. The two structures (§ *What the orchestrator remembers*). Everything below depends on it,
   and item 2's restoration rule falls straight out of it.
3. Item 2 — entry places, exit removes.
4. Wire the decision log. It makes items 3 and 5 measurable rather than argued, and it is what
   turns item 2's lost packets from an anecdote into a per-decision cost.
5. Items 3, 5, 6 in whatever order suits.
6. Item 4 last: it has no consumer until a learning strategy exists, and that is its own round.

Read `discardedToDisconnectedGate` at some point along the way — not as a gate on any decision,
only to confirm the lost-packet metric is not under-reporting.
