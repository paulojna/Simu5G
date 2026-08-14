# Orchestrator internals

Refactor 2 of three. Refactor 1 (the Controller ↔ orchestrator boundary) is done; see
[meo-controller-plan.md](meo-controller-plan.md) for what it settled and why. Refactor 3
(Controller ↔ external model server) is planned in
[controller-model-plan.md](controller-model-plan.md) and appears here only where it
constrains this one.

Naming rule: descriptive names only. No letter labels.

**Landed so far** (2026-08-13), in the order below: `ensureRunDirectory`; the two views —
`userPresence_` on the orchestrator and the promoted registry, with `AppState` and retention;
migration keeping entry identity; the key-space fixes; the decision log wired to all three
strategies and to both migration completion paths; predicted exits log-only (item 2); item 3,
whose two pieces — "the fallback is the old instance" and a way out of *awaiting confirmation*
— came with the state machine and the log; and the queued-migration policy at the end of this
file, now one newest-destination slot per user instead of a queue.

Then item 5, the reactive fallback as a switch on the proactive strategy; item 6, the
configuration authority, with the profiles as ini sections and each module recording what it
derived; and item 4's orchestrator side — the two views readable from a strategy, the
`LearningStrategy` class, the HTTP boundary to the engine, and the `Learning` profile with
its ablation. **Item 4 is written but has never been built or run, and no engine answers it
yet**, so the arm exists as a configuration that fails at startup rather than as a result.
That engine, and the campaign around it, is [model-plan.md](model-plan.md); the experiment
design it sits inside is [experiments.md](experiments.md).

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

- **`ObserveOnly` is dissolved, not built.** It was to be a strategy that hears everything
  and acts on nothing, needed by two runs. Collecting a training set is the `CollectHistory`
  profile (item 6): `RemoveOnExit` plus the Controller recording events and telemetry — not
  migrating is all the neutrality the recording needs, and exits merely clean up after users
  that left. Until the profiles exist, setting `reactionStrategy = "RemoveOnExit"` in the
  collection configuration is the same thing, available today. Measuring a model needs no
  special run at all — see the shadow-mode note in item 1: movement is exogenous to the
  orchestrator, so a real proactive run's events remain clean ground truth and the decision
  log scores every prediction in place.
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

**Most of the application view already exists.** `MecAppRegistry::AppEntry`
(`services/MecAppRegistry/`) holds the contextId, the `appDId`, the application name, the
UE address, the instantiated endpoint (`mecAppAddress` / `mecAppPort`), and the host module —
everything needed to answer "where is this user's application". What it lacks is exactly
what this round needs:

- **A lifecycle state.** Placed, migrating, awaiting confirmation, gone — as a field on the
  entry. Today "migrating" is not representable at all, and "gone" is expressed by erasure.
- **Retention.** `unregisterApp(contextId)` erases the entry, destroying the fact that this
  user ever had an application, which `appDId` it ran, and when it went. An entry kept in
  state *gone*, descriptor intact, is what lets offline evaluation join a deletion to the
  dark period that follows it (item 2), and is what an `instantiate` action from the
  learning engine recreates from (item 4).
- **One key form.** The registry's index (`ueAddressToContextId_`) is keyed by bare IP;
  events carry `acr:`-prefixed addresses, and `MecAppMigrationManager` strips the prefix at
  each of its entry points. When the views are split, convert once at the boundary to a
  single canonical form, or the stripping spreads to every new call site.

So the task is less "build a second structure" than "promote the registry to the
application view" — give it the state field, stop erasing — and then take `userMEHMap` down
to a pure user view fed by events.

What each holds, who writes it, and which view every existing call site meant is decided
here, so implementation starts from a spec rather than a judgment call.

**User view** — replaces `userMEHMap`. Keyed by the canonical user address: bare IP, the
`acr:` prefix stripped once where a message enters the orchestrator, never present inside
either view. One row per user ever observed:

```
currentMEH        // empty = exited; written by events only
lastEventAt       // observedAt of the event that wrote currentMEH
lastObservedMEH   // telemetry enrichment; empty when telemetry is off
lastSampleAt      //   "
```

Writers: the `USER_EVENT` arm of `handleMessage`, and nobody else. Entry and handover set
`currentMEH`; exit clears it but keeps the row — the current comment has this right, "the
user is no longer anywhere" is a fact worth remembering, with its timestamp. Telemetry
consumption fills the enrichment fields and touches nothing else. No strategy and no
lifecycle code writes here.

**Application view** — the promoted registry. `AppEntry` keeps its fields and gains one:

```
state: Placed | Migrating | AwaitingConfirmation | Gone
```

The transitions, all of them: the UALCMP create flow registers → `Placed`. `migrateApp`
accepted, new instance being instantiated, UE not yet retargeted → `Migrating`. New instance
up, endpoint written, UE retarget sent → `AwaitingConfirmation`. Confirmation arrives →
`Placed` on the new host. Confirmed exit → `Gone`, entry kept. Failure or timeout resolves
to `Placed` on whichever host still serves the user — before the retarget that is the old
host (item 3's case); after it, the new host, with the failed teardown logged (the "was the
old instance actually destroyed" fact item 1 wants). No transition leaves `Gone` in this
round; item 4's `instantiate` action adds one later. Writers: lifecycle code only —
`MecAppLifecycleManager` and `MecAppMigrationManager` through the registry interface.
Strategies read, never write.

**The one rule that makes these two views rather than one:** no code path copies a value
from one view into the other, and nothing reconciles them. During a proactive migration the
user view says host A while the application view says host B — that disagreement is the
mechanism working. Every consumer must know which question it is asking: *where is the
user* (user view), *where is the application* (application view), or *do they match* (read
both, compare, act — the learning reward and `checkIfMigrationIsNeeded` are both this
question). A helper answering the third question is fine; a sync that makes it always true
is the bug this section exists to prevent.

**Call-site inventory.** Every current reader and writer, and where each lands:

| Site | Today | After |
|---|---|---|
| `MecOrchestrator.cc:217` (event arrival) | writes `userMEHMap` | user view — unchanged, the only writer |
| `MecOrchestrator.cc:480` (`removeAppFromSystem`) | erases from `userMEHMap` | **deleted** — this is the conflation; presence is the events' fact, not the lifecycle's |
| `MecOrchestrator.cc:64` (initialize) | clears `userMEHMap` | clears both views |
| `LocationSelectionBased.cc:13` (place a new app) | reads `userMEHMap` | user view — correct today, stays |
| `MecAppMigrationManager.cc:91` (`checkIfMigrationIsNeeded`) | reads registry | application view — correct today, stays |
| `MecAppMigrationManager.cc:448–451` (migration swap) | `unregisterApp` + `registerApp` of a fresh entry | becomes a state mutation of the *same* entry — the swap destroys entry identity, which retention and the decision-log join both depend on |
| `MecAppLifecycleManager.cc:185` / `:223` (create / stop) | `registerApp` / `unregisterApp` | `Placed` on create; mark `Gone`, keep the entry, on stop |
| `MecAppLifecycleManager.cc:93` + `MecAppRegistry.cc:54` / `:91` | already-running check probes wrong key spaces | fix specified below — dormant today, load-bearing once item 4's `instantiate` exists |

**The already-running path is broken in two stacked ways — dormant today, load-bearing
tomorrow.** `isAppAlreadyRunning(ueAppId, appDId)` probes `appMap_`, which is keyed by
contextId, with a `mecUeAppID` (`MecAppRegistry.cc:54`) — and a `mecUeAppID` is the
DeviceApp's module ID, a number in the hundreds, while contextIds count 0, 1, 2, …, so the
check is almost always false. On the rare path where it would fire, the recovery lookup
passes an `appDId` string into `findAppByUeAddress` (`MecAppLifecycleManager.cc:93`), which
matches against IP strings, so it finds nothing and the code falls through to instantiate a
duplicate application. The same wrong predicate also guards `registerApp`
(`MecAppRegistry.cc:91`), where a collision would silently refuse the registration and leave
an instantiated application invisible to the orchestrator. None of this bites today because
every UE requests its application exactly once; it stops being dormant the moment item 4's
`instantiate` makes re-creation for a known user a normal operation. The fix is the general
form of the key rule above — **no value from one ID space may probe a map keyed by
another** — applied to the three ID spaces the registry actually has (contextId,
`mecUeAppID`, canonical UE address):

- one lookup per key space: `findAppByContextId` and `findAppByUeAddress` exist; add
  `findAppByUeAppId(mecUeAppID, appDId)` — a linear scan over `appMap_` is fine at these
  sizes;
- `isAppAlreadyRunning` becomes "`findAppByUeAppId` finds an entry whose state is not
  `Gone`", and `registerApp`'s guard uses that same predicate;
- `startApplication`'s branch collapses to one lookup: found → return its `contextId`;
  not found → instantiate. The else-branch that today falls through into a duplicate
  instantiation disappears because check and lookup can no longer disagree.

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

**This join is also how a trained model is scored in the system — no shadow mode needed.** A
"dry run" (decide everything, execute nothing) was considered and rejected, and the reasoning
is kept so it is not re-argued. Shadow mode's premises come from production systems, where a
wrong action hurts real users and shadow traffic is cheap. Neither holds here: the cars drive
their routes whatever the orchestrator does, so migrations never perturb the movement being
predicted — a *real* proactive run's event stream is exactly as clean a ground truth as an
untouched run's — and a dry run costs the same simulation compute as a real one. So the
per-decision join above is the model's in-system scorecard, taken from the same runs that
produce the system metrics. The horizon arithmetic — was the lead time longer than pipeline
plus migration — belongs in the *offline* evaluation of a trained model, before any
simulation is spent on it.

### 2. Exits: confirmed removes, predicted logs

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

**The irreversibility analysis, and where it landed.** Nothing ever recreates a deleted
application: creation is driven by the user requesting one, the user never re-requests, and on
a later event `checkIfMigrationIsNeeded` finds no application and reports that none is needed.
The first answer to this was symmetry — "entry places, exit removes": restore an application to
a user that reappears without one. That answer dissolved under the same test that killed the
shadow mode (item 1): asking which dark users actually differ *between the arms being
compared*.

- **The only mode-specific source was the proactive path executing predicted exits.**
  `MigrateOnPrediction` deletes on a prediction (`MIGRATE_APP_EXIT`); when the prediction is
  wrong the user never leaves, so no exit is ever confirmed, so no entry ever follows, and
  the user is dark for the rest of the run — in one arm only. Executing early bought nothing
  measurable: resources freed a few seconds before the confirmed exit would free them anyway,
  in scenarios where capacity never binds. **Decided: predicted exits are logged as decisions
  and never executed.** Deletion happens only on the confirmed exit. The proactive arm stays
  proactive about *migrations*, where acting early is the entire point.

- **What remains is the false confirmed exit — mode-neutral and accepted.** The Controller's
  exit confirmation runs identically in every profile, so under the same seeds the same false
  exits hit every arm. The confirmation window makes them rare, and their cost is accepted
  rather than repaired: the deletion is a logged decision joined to the user, so offline
  evaluation can see the dark period, attribute it, and exclude that user if it distorts a
  metric. The darkness is genuinely absorbing — an erased user that never changes host is
  never re-announced, since Agents emit on transitions, not on presence — so it ends at the
  next real handover or not at all. Accepted with eyes open, on the grounds that it is
  identical across arms and visible in the logs.

So restoration is not built, and neither is the machinery it would have needed — recreating an
application and telling the UE its new endpoint outside a migration. That endpoint-notification
piece is not dead: the learning action space needs exactly it, and it reappears in item 4 as
part of that boundary.

**Deletion is now safe at the transport layer**, which it was not before. That removed the
crash; it is unrelated to any of the above.

### 3. Migration failure has no path

`MigrateOnChange` carries a literal `// Fallback strategy here?`. Nothing retries, nothing
records the failure as anything but a log line, and the user keeps using the old endpoint.
Refactor 1 gave this boundary the information needed to notice; this round decides what to do
with it. Depends on the application view (§ *What the orchestrator remembers*) existing first.

One case needs handling regardless of policy: a migration whose confirmation never arrives.
The application view's *awaiting confirmation* state has no transition out on that path,
which would make it an absorbing state of its own. A timeout moves the entry to *failed*,
recorded in the decision log as its own outcome joined on `requestNumber`.

**Decided: record, don't retry — the fallback is the old instance.** A retry re-pays the
migration cost against a target that just failed, while the old instance keeps serving at no
additional cost; failure by suboptimality beats failure by repeated disruption. (A retry
policy would also be one more axis differing between arms, muddying the comparison.) So a
failed or timed-out migration writes its outcome to the decision log, moves the application
view back to *placed* on the old host, and does nothing else. The literal answer to
`// Fallback strategy here?` is: there already is one — the instance that never stopped
running.

### 4. Consuming telemetry: the learning strategy and its engine

`reactOnTelemetry` is declared with a default no-op body and no implementation. It receives
`windowStart` / `windowEnd`, the list of hosts that actually reported, and the user and cell
samples.

Note the shape before building on it: a window holds **one row per (user, observing host)**,
which is not one row per user. A user crossing between hosts mid-window is seen by both and
appears twice with different `observedMEH` — the most informative rows in the set. A lost frame
means zero rows, which is why the reporting-host list is carried rather than inferred.

Its consumer is the learning strategy, and that strategy is a thin client: the decisions come
from an external reinforcement-learning engine. What this round owes is the **boundary** to
that engine, settled now so the engine can be developed against it while everything else lands.

**The engine connects to the orchestrator, not the Controller.** This falls out of the settled
boundary rather than preference. The engine's observation must include where the applications
are and what state they are in — the application view, which RAVENS never sees — and what the
engine returns is *actions*, which are the orchestrator's to execute. Routing either through
the Controller would have RAVENS relaying placement it is not supposed to know and commands it
is not supposed to issue. The predictive models stay behind the Controller (refactor 3) for
the mirrored reason: they turn observations into statements about future observations, which
is what RAVENS is for. Models that *describe* live behind the Controller; the model that
*decides* lives behind the orchestrator.

**The loop.** The decision step is the telemetry window — `reactOnTelemetry` is the step
boundary, and events that arrived since the previous window are read as part of the step
rather than acted on one by one (refactor 1's second invariant: immediate delivery does not
imply immediate consumption). Each step, the strategy:

1. assembles the observation — the window's samples, the user view, the application view
   (per user: where the application is and its state), and, when the ablation axis is on,
   the pending predictions;
2. sends it to the engine and receives actions;
3. executes them through the same `IOrchestrationApi` calls every other strategy uses, so
   the decision log and the lifecycle rules apply unchanged.

One run is one episode: a reset marker at initialization, a terminal marker at the end of the
run.

**Decided: the action set is one verb — migrate a named user to a named host.** Waiting is
the absence of an action. `instantiate` and `delete` were considered and dropped, and the
reasoning is kept because it is the same test that killed shadow mode and predicted exits:
ask what actually differs between the arms being compared.

- `delete` buys nothing measurable. It frees resources in scenarios where capacity does not
  bind, so no outcome changes.
- It breaks the comparison. The other arms delete only on a confirmed exit; an arm that may
  delete at will is playing a different game from the baselines it is scored against.
- It is a hole in the reward. The proxy penalises users whose application is misplaced — so
  deleting the application removes the penalty, and the cheapest policy under that reward is
  to delete everyone on the first step.

`instantiate` existed only to make `delete` reversible, so it went with it. **Removal on a
confirmed exit stays, executed by the strategy itself exactly as `RemoveOnExit` does it**, so
deletion remains a rule of the system rather than a decision the agent makes. The engine
decides *where* applications live, never *whether*.

Dropping the pair also removes the one piece of machinery this item would otherwise have had
to build: telling a UE its new endpoint outside a migration. Migration carries its own
notification (`DeviceAppChangeMecHostPacket`, swapped by `UEPerfApp::handleChangeMecHost`),
and with no re-instantiation there is nothing left needing that packet driven by hand.

**Timing is not a separate verb.** The decision step is the telemetry window, so acting on
the window before a handover is a proactive migration and acting on the window after it is a
reactive one. *When* is expressed by which step the engine chooses to act on, which is also
what makes this arm comparable to the other two rather than a different kind of thing.

**Decided: the reward is computed by the engine, not by the orchestrator.** Both components —
users whose application is not on the host observing them, and the cost of each action — are
derivable from the observation the engine already receives, since it carries both views and
the refused actions. Nothing needs to be held on the simulation side, and no `reward` field
crosses the boundary.

The reason to put it there rather than here is that reward shaping is the most-tuned thing in
the project, and a penalty weight should not require rebuilding the simulator. There is also
a real definitional choice inside it — "the host observing them" is `lastObservedMEH`
(telemetry, possibly stale or absent) or `currentMEH` (events) — which belongs on the side
that can be changed freely. The one-step lag the loop was designed around is unaffected: it
is a property of the loop, not of who does the arithmetic.

The reward stays a *proxy*, for the reason that has not changed: training is online, and what
the user actually experienced is not available at runtime — `lostMessages` and `responseTime`
are UE-side statistics that go to the result file. The agent optimises the proxy while the arm
is judged on the real metrics, so the engine logs the reward components per step; if the
learning arm underperforms, offline evaluation must be able to tell a bad method from a
misspecified reward. How training runs relate to evaluation runs is settled in
[model-plan.md](model-plan.md).

**The boundary is HTTP and JSON**, the same transport and library the Controller already uses
for the prediction server: `POST {baseUrl}/reset` opens an episode and returns an id the
server assigns; `POST {baseUrl}/step` carries the observation and answers with actions; the
terminal marker is a step flagged as such. Three things differ from the prediction client
deliberately — one curl handle held for the run rather than one per request, a bounded retry
on transport errors only, and **failure that stops the run**. An unanswered step must never
read as an empty action list: the two are indistinguishable downstream, and a fabricated
all-wait paired with a real transition corrupts what the engine learns from it.

Refused actions are recorded as decisions and handed back in the next observation. An action
that vanishes silently is, to the engine, the same as one it never sent.

What lands in this round is the boundary: the strategy class, the observation and action
shapes, and the `Learning` profile in item 6. The engine itself, and the training campaign
around repeated runs, is the external-model work planned in
[model-plan.md](model-plan.md). What is varied *around* this arm — the oracle baseline, the
information-freshness sweep, and capacity as a load factor — is in
[experiments.md](experiments.md).

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

**The profiles, concretely.** One name per row configures the whole chain; the last column is
derived by the rules below it, never set by hand:

| Profile | Strategy | Orchestrator consumes | Controller duties | Derived |
|---|---|---|---|---|
| `CollectHistory` | `RemoveOnExit` | events | record events + telemetry | Agents: full mode |
| `Reactive` | `MigrateOnChange` | events | record events | Agents: event-only mode |
| `Proactive` | `MigrateOnPrediction` | events + predictions | record events, call the model server | telemetry collected for the server; Agents: full mode |
| `Learning` | learning strategy (item 4) | events + telemetry; predictions as the ablation axis | record events; model server only when the ablation is on | Agents: full mode; engine hook is orchestrator-side |

Two derivation rules do all the propagation:

1. **Agents send telemetry exactly when someone downstream needs it** — the Controller
   records it or forwards it to the model server, or the orchestrator consumes it. Otherwise
   event-only mode.
2. **The Controller calls the model server exactly when the orchestrator consumes
   predictions.**

Events are always on — settled in refactor 1. Mechanically each profile is an ini section
that experiment configurations `extend`; every axis remains overridable underneath, which is
what keeps the learning ablation a configuration change rather than a different profile.

**A run must prove which configuration it was.** Results directories are named by
configuration — that stays the convention — but a name records intent, and the derivation
above decides at runtime. So each module records what it actually derived, one scalar or log
line each: the Agent its mode, the Controller its duties. A run whose derivation went wrong
is then caught from its own output instead of trusted from its directory name — intent and
effect have differed here before, and the run's own record is what settled it.

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

1. `ensureRunDirectory`, and point the collection configuration at `RemoveOnExit` — the
   first because nothing can be trusted before it, the second because it is the one-line
   change that stops migrations under recorded runs now that no `ObserveOnly` strategy is
   needed.
2. The two structures (§ *What the orchestrator remembers*). Everything below depends on it.
3. Wire the decision log — before item 2, not after. Item 2 changes what the proactive path
   does with predicted exits, and the log is what makes its before/after runs comparable;
   landed the other way around, the first runs under the new behavior are exactly the ones
   with no record of it. It also makes items 3 and 5 measurable rather than argued, and
   turns item 2's accepted dark periods from an anecdote into attributable, excludable rows.
4. Item 2 — stop executing predicted exits. With the log in place this is small: the branch
   where `MigrateOnPrediction` schedules deletions today becomes a log-only decision.
5. Items 3, 5, 6 in whatever order suits.
6. Item 4 splits. Its boundary — the observation, action and reward shapes, and the
   orchestrator-side engine hook — should be settled as soon as the two structures exist,
   because the external engine is developed against it. Its implementation stays last; the
   engine itself, and the predictive models, are the external work that follows this whole
   round.

Read `discardedToDisconnectedGate` at some point along the way — not as a gate on any decision,
only to confirm the lost-packet metric is not under-reporting.

---

## Problem found: queued migrations chase the user

Found while reviewing refactor 1, by asking what the orchestrator does with two handovers for
the same user in quick succession.

**Concurrency is already prevented, and that part is right.** `MecAppMigrationManager` keeps
`standByUeIndex_` (user → in-flight request) and `pendingMigrations_` (a queue per user). A
second `migrateApp` for a user whose application is already migrating is queued rather than
started (`MecAppMigrationManager.cc:163`).

**Queueing is the wrong policy.** A user moving A→B→C within a couple of seconds produces two
handovers. The A→B migration runs its full `migrationTime_`, completes, and only then is B→C
dequeued and run. The application spends two migration times to reach C, and passes through B
— a host the user left long before it arrived — as an intermediate step nothing wanted.

Only the newest target has ever mattered. A queue is precisely the wrong structure for that: it
preserves targets already known to be invalid, and being a `std::queue`, several can accumulate.

**The same module already holds the opposite policy on the other stream.**
`MigrateOnPrediction` handles a second prediction for the same user by keeping the existing
scheduled one when the target agrees and cancelling it when the target differs. Two opposite
answers to the same question — what to do when new information supersedes a pending move —
live in one module, decided independently on each stream.

**Where it belongs: item 3 and the application view.** Today the only available response is to
queue, because "a handover arrived while this application is in flight" is not representable —
there is no *migrating* state to test. The `Migrating` / `AwaitingConfirmation` states make the
situation expressible, and the policy is then a decision rather than a consequence of the data
structure. The decision log gives it a place to record what it chose and what that cost.

**Two things settled while finding it, recorded so they are not re-argued.**

*Handovers are not confirmed the way exits are.* The two are different kinds of uncertainty. An
EXIT is ambiguous — one host reporting it cannot distinguish *left the system* from *moved next
door*, and the confirmation window exists to resolve exactly that. A HANDOVER carries positive
evidence from the host that now sees the user; there is nothing to disambiguate, only speed.
Delaying handovers would also slow the reactive arm, which is the baseline the other arms are
measured against.

*This is orchestrator work, not Controller work.* RAVENS answers whether the user moved, and it
did. Whether it is worth moving the application depends on migration cost and on the
application's current state, and `migrationTime_` is an orchestrator parameter — debouncing in
the Controller would mean telling RAVENS what orchestration costs, which is what this boundary
exists to prevent.
