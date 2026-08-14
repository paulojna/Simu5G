# The experiment design

What is varied, what is held fixed, and what each arm is supposed to demonstrate. Companion
to [model-plan.md](model-plan.md), which owns the seed ledger and the training lifecycle, and
to [meo-plan.md](meo-plan.md), which owns the orchestrator internals the arms differ in.

Nothing here restates the seed pools — the 30 / 50 / 40 split, the wall around the
evaluation 30, and the stacking rule between the predictive and RL pools all live in
model-plan.md and are not reopened.

Naming rule: descriptive names only. No letter labels.

---

## The claim the experiments have to support

Recorded first, because two of the arms below exist only to serve it.

Work on MEC service migration generally assumes the orchestrator has instantaneous, exact,
complete access to network state — positions, associations, cell load — at the moment it
decides. The acquisition of that state is not modelled. This system does model it, and every
term is measured rather than assumed:

- `observedAt` is carried end to end, so staleness is a number rather than an inference from
  configured intervals;
- `confirmedMEH` lags `observedMEH` by the reporting delay plus the exit confirmation window;
- telemetry arrives in windows, so the orchestrator sees the world at `telemetryInterval`
  granularity, not continuously;
- a lost frame yields zero rows, not stale rows, which is why the reporting-host list is
  carried explicitly.

The consequence composes into one arithmetic chain:

```
usable lead time = prediction horizon
                 − acquisition delay      (the RAVENS pipeline)
                 − decision delay         (telemetry window granularity)
                 − migration time         (migrationTime_, 12 s today)
```

which is the same inequality model-plan.md checks offline as `H ≥ T + D + S + M`. Prediction
is therefore not "accuracy is good" — it is the mechanism for buying back the horizon that
observation itself consumes. And because that compensation is uncertain, something has to
decide when it is worth acting on, which is what the learning arm is for.

Three components, one argument. The oracle arm and the freshness sweep below are what turn
the first paragraph from an assertion about other people's work into a number in this one.

---

## The arms

| Arm | Strategy | Consumes | Role |
|---|---|---|---|
| `Oracle` | reactive, ground-truth fed | the recorded true association | upper bound; the assumption the literature makes |
| `Reactive` | `MigrateOnChange` | events | baseline under real observation |
| `Proactive` | `MigrateOnPrediction` | events + predictions | prediction as horizon compensation |
| `Learning` | learning strategy | events + telemetry (+ predictions, ablated) | learned trust in the prediction |

`Reactive`, `Proactive` and `Learning` are the profiles already defined in meo-plan item 6.
The learning ablation — predictions on versus off, everything else identical — stays a
configuration change, never a profile of its own.

### The oracle arm is new work

**Purpose: measure the cost of the information plane in this system, rather than arguing
about it.** The orchestrator acts on the true UE-to-gNB association, with no reporting delay,
no confirmation window, no window granularity and no loss. That is precisely the assumption
being criticised, run here.

The gap between `Oracle` and `Reactive` is then the measured price of acquiring information,
under otherwise identical policy. **Both run the same reactive rule** — `MigrateOnChange`,
unchanged — so only the freshness of what they react to differs. If the policy differs too,
the arm measures two things at once and neither can be read off the result.

**The source is the recorded association.** During `CollectHistory` runs each UE's serving base
station is written to the vector file; the oracle arm replays that trace as its event stream.

**Why a recorded trace is valid ground truth for a different run:** mobility is exogenous. Cars
drive their routes whatever the orchestrator does, and handovers follow from radio and position
rather than from where an application happens to run — the same property that makes a real
proactive run's event stream clean ground truth (meo-plan item 1). So an association trace taken
under `CollectHistory` holds for every arm on the same route file.

**What has to be added first: the association is not recorded today.** `masterId_` in
`LtePhyUe` / `LtePhyUeD2D` holds the UE's serving base station and is updated on handover, but
nothing emits it, and there is no `servingCell` statistic anywhere in the tree. It needs a
signal emitted on association and on every handover, declared as a vector statistic in the UE's
NED, and the scenario's blanket vector filtering opened up for that name — an unrecorded
statistic reads as one that never changed.

*A variant was considered and dropped: reading each host's Location Service directly, skipping
only the Agent, Controller and confirmation window. It isolates the aggregation cost while
holding data quality constant, but it is not what this arm is for — recorded so it is not
re-proposed.*

**Risk, and it is real:** if `Oracle` barely beats `Reactive`, the free-information assumption
is benign in these scenarios and the framing above loses its force. Worth knowing before the
campaign is built around it. One route file, oracle versus reactive, is enough to find out.

---

## Axis: information freshness

`telemetryInterval` is already a propagated, derived parameter (meo-plan item 6), so this
costs a configuration sweep and no code.

Run the arms at several intervals — 1 s, 3 s, 5 s — and plot the metrics against it. The
figure this produces is the direct evidence for how much the idealization matters: not that
delay exists, but how steeply the results depend on it.

Note that the two current configurations already disagree — `RavensControllerApp.ned`
defaults to 3 s while the `tust_1to1` ini sets 1 s — so the sweep also settles which is the
reference point rather than leaving it to whichever file was edited last.

**Same deflation risk as above:** a flat curve means freshness does not matter here.
Cheap to check early.

---

## Axis: capacity, as a load factor

**Decided during design: capacity becomes an experimental axis rather than a fixed slack
value.** With capacity effectively unbounded — `maxMECApps = 250`, `maxRam = 16GB` against a
few tens of applications per host — no action any arm takes is ever constrained by
resources. The optimal policy collapses towards "keep the application where the user is",
which is what `Reactive` already does by construction, and the learning arm has little
headroom to demonstrate.

Under contention the problem becomes genuinely different: the agent must arbitrate *which*
users get the nearby host, and migrating aggressively becomes self-defeating.

### Bind on RAM, not on the application count

Enforcement lives in two places and only one of them is visible to the selection policy:

- `VirtualisationInfrastructureManager.cc:151` tests `currentMEApps < maxMECApps`, but that
  is *inside* instantiation;
- `isAllocable(ram, disk, cpu)` is what `LocationSelectionBased::findBestMecHost` consults
  before choosing a host (`LocationSelectionBased.cc:33`).

So a limit expressed as an application count is invisible to the orchestrator: it would
select a host that then fails at instantiation. **RAM is the binding resource; `maxMECApps`
stays slack.** `maxMECApps` additionally sizes gate arrays
(`setGateSize("meAppOut", maxMECApps)`), which is a second reason not to use it as a knob.

Only one resource binds at a time, deliberately — with two, an observed effect cannot be
attributed to either.

### The cloud fallback is already the penalty

When the closest host cannot allocate, `findBestMecHost` falls through to
`findBestHostByResources`, which picks the host with the most free CPU. With
`maxCpuSpeed = 10000000` and `maxRam = 1000TB`, that is always `mecHost11` — the cloud.

So contention already produces the right consequence without building anything: edge full →
application placed in the cloud → higher latency → visible in `responseTime`. The cost the
agent learns to avoid is physical rather than a term invented in the reward.

### The levels

```
ρ = peak concurrent applications on the busiest edge host
    ─────────────────────────────────────────────────────
    applications that fit in one edge host's RAM
```

| Level | ρ | What it tests |
|---|---|---|
| Unconstrained | ≪ 1 | current setup; a pure timing problem |
| Tight | ~0.9 | contention during transients and migrations only |
| Binding | ~1.3 | the busiest host cannot hold everyone; arbitration required |

Only the third forces a choice *between* users.

### Calibration

1. Record `CollectHistory` runs on the training pools — already being produced for the
   predictive models, so this is free.
2. From the telemetry CSV, compute concurrent users per host per second.
3. Take the peak across edge hosts, pooled over route files.
4. Set edge `maxRam` so peak ÷ capacity lands on each target ρ.

**One capacity value across all runs, not one per route file.** A deployed host has fixed
capacity and variable traffic; per-scenario tuning would be a confound. Calibrate from the
training pools, then report the resulting ρ *distribution* over the evaluation 30 as a
property of the experiment rather than a parameter of it.

**Count the migration overlap.** During a migration the application can exist on both hosts
at once, so peak occupancy measured without it understates ρ. Under a tight ρ some
migrations will then fail for want of room — not a defect: meo-plan item 3 already decided
that outcome (record it, keep serving from the old instance), and it is exactly the tradeoff
the agent should discover.

### Consequence for the learning observation

Under a binding ρ, per-host utilization stops being inert and becomes a real feature. Each
candidate host in the observation carries a **ratio**, not absolute bytes — comparable across
heterogeneous hosts and still meaningful when ρ changes between runs:

```json
{ "host": "mecHost4", "ramUtilization": 0.82, "isCloud": false }
```

Absolute resource usage is *not* sent: every UE runs the same application, so usage is a
fixed multiple of the application count, which the observation's application view already
carries. Sending both is the same number twice in different units.

---

## Generalization, at the feature level

model-plan.md handles this at the seed level — pool walls, the validation holdout, the
specialization check. One thing remains that no seed discipline can catch.

Mobility is **deterministic given a route file**: the OMNeT++ seed varies radio and timing,
not trajectories. So repeated exposure to a training pool invites memorization of specific
routes rather than of situations.

**The rule: the feature encoder never sees anything identifying the episode, the vehicle, or
the absolute moment.**

| Never a feature | Use instead |
|---|---|
| absolute simulation time | time since last event, time since last migration |
| user address, vehicle index | nothing — the row is anonymous |
| episode and step index | nothing — control-plane only |
| raw `x`, `y` on a fixed map | distance and closing rate per candidate host |

Raw coordinates are the subtle one: on a fixed map they are a route fingerprint.
`distanceToAccessPoint`, `speed`, `bearing` and `rsrp` carry the same physics without it.

**This is a rule about features, not about the wire.** Actions come back addressed to
specific users, so the user address and absolute timestamps must travel in the payload. They
are addressing metadata that the encoder is forbidden to consume. Enforcing it on the Python
side rather than in C++ also keeps a feature change an edit rather than a recompile.

What *is* kept: topology. Serving cell, candidate hosts, which host the application is on.
The action is "migrate to host X" and cannot be expressed without naming hosts; a cell
identifier recurs across every route file, so it is map structure rather than an episode
fingerprint.

**The check that this worked** is the train/test gap: score the frozen policy on the
training pool and on the evaluation 30. Clearly better on the former means memorization, and
*that* is the point at which generating more route files helps. Until it is measured,
generating routes is speculative.

---

## Open, and blocking nothing yet

- **How many users per run.** model-plan.md's arithmetic assumes ~13 concurrent; the
  scenario may carry substantially more. The convergence conclusion is unaffected either way
  — one pass over the RL pool is hundreds of thousands of transitions on either figure — but
  the ρ calibration above depends on the real number, so it has to be measured rather than
  estimated.
- **Wall-clock per 3600 s execution.** The binding constraint on the whole campaign, and
  still unmeasured. It decides whether parallel executions are needed, not whether the
  design is right.
- **The ρ values.** 0.9 and 1.3 are placeholders until the calibration runs exist.
