# Controller ↔ Orchestrator

Refactor 1 of three. The other two — the orchestrator's internals, and the
Controller ↔ external model server relation — are sketched here only where they
constrain this one.

Naming rule: descriptive names only. No letter labels.

---

## Scope

What the Controller sends the orchestrator, when, and how the orchestrator's
configuration is expressed. Nothing about how decisions are made once the
messages arrive.

---

## What this replaces

Today the Controller accumulates *transitions* ("device 7 moved from A to B") in
a table keyed by device address, and drains it every 2 seconds. Two moves for the
same device inside one window overwrite each other, so the `from` field can be
wrong. It is currently harmless — `MecAppMigrationManager` looks the device up in
its own registry and ignores the `from` it was handed — but the logs are wrong and
nothing enforces that the value stays unread.

The fix is not to merge transitions more carefully. It is to stop accumulating in
a container that can lose things.

---

## Three streams

The Controller publishes three independent streams. A profile is defined by which
of them are switched on, not by a single mode name. Any combination is valid; the
named profiles below are just the combinations we intend to run.

| Stream | Carries | Timing |
|---|---|---|
| **Events** | Confirmed placement changes | Immediate, as confirmed |
| **Predictions** | Expected future changes | Periodic |
| **Telemetry** | Raw per-device observations | Periodic |

**Events are immediate.** This mirrors the Agent → Controller design, where state
changes ride a reliable channel the moment they are confirmed and bulk data rides
a periodic one. Applying the same split at this boundary means one idea in two
places, and it stops the second hop giving back the latency the first hop was
built to save.

**Events stay on in every profile.** They are the only stream that reports what
actually happened, which makes them the correction channel for a wrong prediction
and the reward signal for a reinforcement-learning agent.

**Everything else is periodic.** Predictions and telemetry arrive on a fixed tick,
which becomes the orchestrator's decision step: one coherent picture per interval.
Strategies that do not read a stream simply ignore it.

---

## Message shapes

Events:

```
{ deviceId, eventType, fromHost, toHost, observedAt }
```

`eventType` is entry / handover / exit. `observedAt` is when the Agent observed
the change, not when the Controller forwarded it.

Predictions:

```
{ deviceId, fromHost, toHost, expectedAt, confidence, modelId }
```

`modelId` is a label for logging and comparison only. The orchestrator must not
branch on it — which model produced a prediction is the external server's
business, and keeping that true is what lets refactor 3 add models without
touching this boundary.

Telemetry: the existing `UserSample` structure, verbatim. There is already one
canonical per-device record shared by the file-writing path and the external
server, built specifically so the two cannot drift. A third shape here would
reintroduce that problem one level up — the reinforcement-learning agent would be
fed something subtly different from what the models were trained on.

---

## Two invariants

- **The periodic message always reflects every event already sent.** Same module,
  same state map, so this is free — but an ordering inversion here would be
  miserable to debug.
- **Immediate delivery does not imply immediate consumption.** A strategy with a
  fixed decision step accumulates events and reads them at its next boundary. The
  protocol's job is only to avoid *adding* delay.

---

## Configuration: three axes, not one name

Today a single `mode` string (`SaveDataHistory` / `NotifyOnDataChange` /
`SendToExternalServer`) decides three unrelated things at once: what the Agents
send, what the Controller does with what arrives, and what the orchestrator hears.
Combinations that are perfectly reasonable cannot be expressed — record to file
*and* let the orchestrator react; use the external server *and* keep the recording;
run the external server with the orchestrator ignoring it, to measure prediction
quality without paying for migrations.

Adding "which model" as a fourth axis to the same string multiplies it out
(`SendToExternalServer_LSTM`, …) and gets worse with every combination.

Split into independent settings:

- what the Agents send (events only / events and telemetry)
- what the Controller does with telemetry (discard / write to file / forward to
  the model server) — these are not mutually exclusive
- which streams the orchestrator receives

The existing profiles then become named combinations of these, not values of one
setting.

---

## Orchestrator modes

Three, differing only in which streams they consume:

- **Reactive** — events only.
- **Proactive** — events and predictions. Events remain the correction path when a
  prediction is wrong or arrives too late.
- **Reinforcement learning** — events and telemetry, **and predictions as well**.
  Feeding the agent model predictions alongside raw observations is expected to
  help, but it is not assumed: whether predictions are on is an **ablation axis**,
  run both ways. This is the concrete reason the streams must be independently
  switchable rather than bundled into a profile name — the ablation is a
  configuration change, not a code path.

Because the modes differ only in consumption, they are comparable by construction.

---

## Timing constraint the orchestrator imposes

For a proactively migrated application to be in place before the device arrives,
the model's prediction horizon `H` must satisfy:

```
H  ≥  T + D + S + M
```

`T` telemetry interval, `D` model inference time, `S` Controller → orchestrator
interval, `M` migration duration (~12 s in our scenarios). With T=3, S=1, D≈0.5
this needs a horizon of roughly 17 seconds.

`MigrateOnPrediction` already computes `adjustedDelay = targetTime − now −
migrationTime_` and clamps it at zero. **That clamp firing is exactly the case
where the horizon lost to the pipeline.** Counting it is a one-line, per-run
measurement of whether the model is predicting far enough ahead.

---

## Open decisions

**Does the periodic message carry a placement snapshot?**

*For:* the orchestrator already keeps a placement map (`userMEHMap`) and already
uses it to choose where to place a new application
(`LocationSelectionBased::findBestMecHost`). That map is not going away; the only
question is whether the Controller authors it or the orchestrator rebuilds it by
replaying events. Rebuilding is the orchestrator doing the Controller's job, and
it can drift with no way to recover.

*Against:* separation of concerns — the Controller knows where things are, the
orchestrator acts. Publishing full state blurs that.

*Possible resolution:* events trigger action, the snapshot is reference only. The
orchestrator never acts because the snapshot changed; it acts on events, and reads
the snapshot when it needs to know where something is. If the snapshot goes in, the
diff base must be a **separate map from `userMEHMap`** — the orchestrator mutates
that one itself (`removeAppFromSystem` erases from it), and a proactive migration
moves the application before the device moves, so conflating the two would make the
orchestrator read its own migrations back as device movement.

**What happens when a migration fails?** `MigrateOnChange` has a literal
`// Fallback strategy here?`. Nothing retries. Belongs to refactor 2, but this
boundary decides whether the orchestrator has the information to notice.

---

## Small items in scope

- `USERS_UPDATE` and `MIGRATION_PLAN` are `#define`d separately in
  `RavensControllerApp.cc` and `MecOrchestrator.cc`. Move to a shared header, in
  the style of `RavensLinkProtocol.h`. `MigrateAppMessage::setType(0/1)` is the
  same problem — 0 means migration, 1 means exit, documented only in a comment.
- The Controller declares an `inGate` and the orchestrator a `toRavensController`
  gate, but `handleMessageWhenUp` has no branch for either, so anything sent back
  falls through to the drop case. Wire it or remove it.
- `getMecHostIdFromAccessPointId` is declared and never defined, which is why the
  access-point list stored in the Controller's `mehStateMap` is never read. The map
  itself *is* read (host lookup during the handshake); only its access-point payload
  is dead.

---

## Out of scope

- How the orchestrator decides anything (refactor 2).
- How the Controller talks to the external model server, and how multiple models
  are registered and selected (refactor 3).
- The Agent → Controller telemetry interval and sampling rate. Being changed
  separately: batching is being removed, one observation per device per frame.
