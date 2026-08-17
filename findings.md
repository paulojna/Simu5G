# Session findings

What was built, what was measured, and what is still open. Companion to
[experiments.md](experiments.md) and [meo-plan.md](meo-plan.md).

All numbers below come from the 300s test configurations at seed 0, 58 cars.

---

## 1. The oracle arm was built

The oracle replays the handovers that actually happened, delivered early enough that each
migration finishes as the car arrives. It runs `MigrateOnPrediction` unchanged, fed from a
file instead of from the model server, so the orchestrator cannot tell the two apart.

New: `OracleTraceSource` in the Controller's sinks, two Controller parameters
(`oracleTraceFile`, `oracleLeadTime`), and `Oracle` / `T7` / `P5` configurations.

**Nothing needed adding to the PHY.** `servingCell` already existed upstream — emitted on
attach (`LtePhyUe.cc:225`), on handover (`LtePhyUe.cc:522`, `NRPhyUe.cc:458`) and on every
mobility update (`LtePhyUe.cc:739`), declared as a vector in `LtePhyUe.ned:32-33`, and already
unblocked by `omnetpp.ini:59`. experiments.md said the opposite and has been corrected.

Each car carries two PHY modules. The LTE `phy` records a flat 0; the trace must read `nrPhy`.

---

## 2. A car's IP is not stable between runs

A UE's address is `addressBase + its OMNeT++ module id` (`HostAutoConfigurator.cc:52`), and
module ids count every module ever created — including the MEC applications the orchestrator
creates and deletes as a run proceeds. Different arms create different numbers of them.

Measured across three pairs of runs: **7, 8 and 12 of 58 cars kept the same IP.**

The Veins car index is stable, because it comes from SUMO insertion order
(`nextNodeVectorIndex++`). So the trace keys on the car index and the address is resolved
inside the run that uses it.

**Rule for all analysis: never join two runs by UE address.** Join by car index, and take
addresses from that run's own `.sca`, where `UEPerfApp` records them as `ip_0`..`ip_3`.

---

## 3. Arms did not reproduce each other — the main finding

Two arms at the same seed drive identical routes, but they did not produce identical
handovers. Cars are created at identical times; only the radio differed.

| Attempt | Same cells and times | Same cells, shifted | Different cells |
|---|---|---|---|
| As found | 24 / 58 | 22 / 58 | 12 / 58 |
| Channel model on a shared dedicated stream | 31 / 58 | 18 / 58 | 9 / 58 |
| One stream per car | 23 / 58 | 21 / 58 | 14 / 58 |
| **Shadowing and fading off** | **58 / 58** | 0 | 0 |

Time shifts among the "same cells" rows ran to a median of 2s and a maximum of 10s — against
a 12s migration and a 13s lead time.

### What was ruled out

- **Cell load coupling.** `downlink_interference` defaults to false and the ini never enables
  it, so the handover decision is not affected by what other cells are doing.
- **Random stream sharing.** Giving each car its own stream was verified to actually take
  effect — `ancestorIndex(2)` resolved correctly, confirmed by an out-of-range error naming
  `car[1]` asking for stream 8 — and it did not help.

### The actual cause

Fading paths are drawn once per car and then fixed, so their count cannot drift. **Shadowing
is redrawn whenever a car has moved 50m, but that check only runs when a packet arrives.**
Different arms deliver different packets at different moments, so the redraw points land
differently. Per-car streams cannot fix this: the car itself is asking at different times.

### Side finding: a limit on `num-rngs`

OMNeT++ builds each seed as `(seedSet * numRngs + rngId) * 32768` in 32 bits
(`cmersennetwister.cc:54`). Past 131072 it wraps and repetitions silently share seeds. With
120 seed sets, `num-rngs` must stay under about 1092. A per-car mapping would have needed
~1420 (4568 vehicles depart before 3600s, scaled by SUMO's `scale 0.31`). It would have
overflowed. Now reverted to 7.

---

## 4. The fix: a deterministic sub-experiment, not a global change

Radio randomness is disabled in `[Config DeterministicRadio]`, which only the oracle pair
extends. Everything else — collection, Reactive, Proactive, Learning — keeps full fading and
shadowing.

**Why not global.** With fading off, `Rsrp` becomes a deterministic function of
`DistanceToAccessPoint`, a column the training data already contains. Delivery ratios flatten
towards constant. The collection runs exist to produce rich training data, so they keep it.

**Why the oracle needs it.** Every other arm works out its own events inside its own run, so
radio randomness is variance that repetitions absorb. The oracle is the only arm that imports
events from a different run, and with randomness on those events are not noisy but *wrong* —
14 of 58 cars were being told to move to cells they never visited.

**The pivot.** `Reactive` appears in both sub-experiments, which is what connects them:

```
Oracle − Reactive(deterministic)   = the budget: what perfect information is worth
Proactive − Reactive(realistic)    = how much prediction recovers of it
Learning  − Reactive(realistic)    = how much the learned policy recovers
```

`T8` / `P6` exist to be the deterministic half of that subtraction, and set against `T2` / `P2`
they also measure how much the deterministic radio changed reactive behaviour at all. That
second reading is what licenses carrying the budget across.

**Trace source.** Traces must come from a run under the same radio conditions, so they come
from `T8` / `P6`, not from the collection runs. Not circular: under deterministic radio the
association follows position alone, so the reactive arm's association is also the oracle's.
Order is T8 → build traces → T7.

---

## 5. Results

### Response time

| | mean | median | p90 | p99 | handover migrations |
|---|---|---|---|---|---|
| T2 Reactive (fading) | 55.72 ms | 41.0 | 75.0 | 635.0 | 52 |
| T8 Reactive (deterministic) | 40.27 ms | 41.0 | 65.0 | 87.0 | 38 |
| T7 Oracle (deterministic) | 37.17 ms | 41.0 | 47.0 | 81.0 | 38 (32 proactive, 6 reactive) |

Both deterministic arms saw exactly 38 handovers, so they are directly comparable. The oracle
handled 84% of them before they happened.

### The overall mean understates it badly

Splitting each run into samples near a handover (2s before to 20s after) and the rest:

| | baseline | near handover | excess | share near |
|---|---|---|---|---|
| T2 Reactive (fading) | 48.10 ms | 87.33 ms | 39.23 ms | 19.4% |
| T8 Reactive (deterministic) | 35.37 ms | 63.48 ms | 28.11 ms | 17.4% |
| T7 Oracle (deterministic) | 35.64 ms | 44.45 ms | **8.82 ms** | 17.4% |

Three things:

- **T8 and T7 have the same baseline** (35.37 vs 35.64). Away from handovers they are the same
  system, which is the check that nothing else differs between them.
- **The oracle removes 69% of the handover penalty** (28.11 → 8.82 ms). That is the headline,
  not the 3.1 ms difference in overall means.
- **T2's baseline is 12.7 ms worse than T8's.** That has nothing to do with orchestration — it
  is fading degrading the link everywhere. Comparing T2 and T8 means head-on conflates the
  radio with the strategy.

Only 17% of samples sit near a handover because most cars barely hand over: 19 of 53 never do,
and the distribution is 0:19, 1:22, 2:11, 3:1 over an average 87s of app activity.

---

## 6. Handover flapping

Simu5G has **no time-to-trigger**. It compares the instantaneous signal against a power margin
and switches immediately (`LtePhyUe.cc:322`). Real networks smooth the measurement and require
the condition to hold first. So the model bounces at cell edges more than reality would.

| | trace moves | shortest dwell | ping-pong pairs |
|---|---|---|---|
| fading on | 67–76 | 1s | 12–17 of 30–38 |
| fading off | 51 | 4s | 7 of 15 |

**RAVENS already absorbs much of it**, through `entryConfirmSamples` / `exitConfirmSamples = 2`
at the Agent and the 3s exit confirmation window at the Controller:

| | PHY handovers | RAVENS confirmed | filtered |
|---|---|---|---|
| T2 (fading) | 78 | 52 | 26 (33%) |
| T8 (deterministic) | 51 | 42 | 9 (18%) |

**Decision: do not change the PHY.** Filtering in RAVENS is the better place — "do not move an
application for a visit shorter than the migration takes" is an orchestration rule, defensible
on its own terms, and it stays correct if Simu5G ever gains a real time-to-trigger. The cost is
that filtering means waiting, and waiting is the quantity being measured, so the confirmation
parameters become an experimental axis rather than constants.

T2 is therefore a *pessimistic* reactive baseline, penalised by a handover model more
trigger-happy than the standard.

---

## 7. Lead time

`MigrateOnPrediction` keeps one pending prediction per user, so a second one arriving first
cancels it and the car skips a host — with nothing in the results showing anything went wrong.

```
lead time > migrationTime (12s)                   or the migration finishes late
lead time < migrationTime + shortest dwell        or predictions overwrite each other
```

Default is 13s. Deterministic traces have a shortest dwell of 4s, giving a ceiling of 16s, so
13s is safe. `OracleTraceSource` warns at startup if the configured value falls outside the
range for the trace it loaded, and the trace script reports the shortest dwell.

---

## 8. ravens-lab

New sibling repository for everything that is not the simulator: reading results, building
oracle traces, model training, figures.

```
ravens-lab/
  ravenslab/results/   find a run, read its vectors and scalars via opp_scavetool
  ravenslab/traces/    build the oracle traces
  scripts/make_oracle_traces.py
```

Two conventions worth keeping: a run is addressed by configuration and repetition, never by
filename (the files are called `$0="fair"-0.vec`); and nothing joins two runs by UE address.

Recorded names carry their recording mode — the statistic is `servingCell:vector`, the scalars
are `ip_0:last`. Filters must spell that; the readers strip it on the way out.

---

## 9. Open

**For the supervisor.**

- A deterministic branch of every profile, not just the oracle pair. Makes all arms directly
  comparable and removes the pivot argument. Affordable if the model uses **mobility features
  only** — `x`, `y`, `Speed`, `Bearing`, `DistanceToAccessPoint` are identical under both radio
  settings, so one model serves both branches and the radio features become a feature ablation
  rather than a second model family. Learning stays realistic-only: a deterministic world
  removes the uncertainty it exists to learn about.
- Whether to sweep the confirmation parameters as a staleness-versus-stability axis.

**Done this session.**

- `latePredictions` is now recorded as a scalar. `ReactionOnUpdate` gained an `onRunFinished()`
  hook, called from `MecOrchestrator::finish()` while the module is still whole;
  `MigrateOnPrediction` overrides it. The stdout print in its destructor is gone. Zero is the
  expected value for the oracle arm.

**Smaller.**

- Controller and orchestrator scalars were being discarded by `**.scalar-recording = false`;
  exceptions added, but no run has used them yet. The same applies to `latePredictions` below:
  it is recorded now, but has not been produced by a run.
- The 6 handovers the oracle missed are unexplained. Most likely cars whose first handover
  falls within the 13s lead of their appearing, so the prediction comes due before the car
  exists.
- `num-rngs` must stay fixed for a whole campaign: the seed formula includes it, so changing it
  re-seeds everything and invalidates collected runs.
