# External models and the experiment schedule

Companion to [meo-controller-plan.md](meo-controller-plan.md) (refactor 1, done) and
[meo-plan.md](meo-plan.md) (refactor 2, the orchestrator internals). This file covers what
happens *outside* the simulator: training the predictive models (LSTM, XGBoost), training
the reinforcement-learning engine, and how the run and seed budget is spent. The boundaries
these models plug into are owned by the other files — the prediction server behind the
Controller (refactor 3), the learning engine behind the orchestrator (meo-plan item 4) —
and are not reopened here.

Naming rule: descriptive names only. No letter labels.

---

## Two currencies: seeds and executions

Everything below rests on not confusing these.

- **A seed** is one distinct mobility scenario. There are 120. Seeds are spent by
  *distinctness requirements* — who is allowed to have seen what — and nothing else.
- **An execution** is one simulation run. Executions cost compute time only, and the same
  seed may be executed as many times as needed.

The consequence that breaks naive accounting in both directions: evaluation replays the
*same* seeds once per arm, so it costs few seeds but many executions; RL training cycles
its seeds repeatedly, so it costs few seeds but arbitrarily many executions. Neither is
"30 runs".

---

## The seed ledger

| Seeds | Pool | Rule |
|---|---|---|
| 30 | **Evaluation** | Untouched by everything that learns. Every arm replays exactly these, frozen and deterministic. |
| 50 | **Predictive pool** | Recorded by `CollectHistory`. The LSTM and XGBoost pipelines — training, validation, hyperparameter tuning, *and* the offline test split — live entirely inside these. |
| 40 | **RL pool** | Learning-profile live runs, cycled until the reward plateaus. The predictive models see these seeds only at inference time, never in any training or tuning role. |

The one hard wall is around the 30: nothing that learns may ever see them, and no
mid-training progress check may peek at them — each peek quietly turns them into validation
data. A progress check belongs inside the pools (see the validation holdout below).

The wall between the 50 and the 40 exists for a subtler reason, recorded next so it is not
re-litigated.

**Execution count, for planning wall-clock time:** evaluation is 30 × the number of arms
(Reactive, Proactive, RL with predictions, RL without = 120 executions), plus 50 corpus
recordings, plus an elastic number of RL training executions (hundreds, in passes over the
40-seed pool). The total is compute, not a scarce resource; the only scarce resource is the
seed ledger above.

---

## The stacking rule: why the predictive and RL pools must not overlap

The RL agent's observation includes the LSTM's predictions, and the agent *learns how much
to trust them*. That trust is calibrated against the prediction quality experienced during
training. If the agent trained on seeds the LSTM also trained on, it would experience
training-set-quality predictions — flattering, since the LSTM has effectively seen those
trajectories — and learn to follow them with confidence they do not deserve on unseen data.
At evaluation the predictions drop to test-set quality, the learned trust is miscalibrated,
and the RL arm underperforms for a reason that is invisible everywhere except the final
comparison, where it silently biases the result against the learning arm.

This is the standard rule for stacked models — a model consuming another model's outputs
must be trained on *out-of-sample* outputs — applied to the seed ledger:

- The LSTM's entire pipeline stays inside the 50. "Never trained on" includes validation
  and early stopping: tuning against the RL pool is a milder form of the same leak.
- The RL agent then sees out-of-sample predictions during training (on the 40) and again at
  evaluation (on the 30). The conditions are consistent end to end, which is what keeps the
  learned trust valid.
- Both RL variants of the ablation (with and without predictions) train on the same 40, so
  the ablation compares the prediction stream, not the training data.

---

## The RL training lifecycle

Training is online — decided in meo-plan item 4 — and continuous across runs: the engine is
a persistent server that outlives any single simulation. Each run connects, sends the reset
marker, streams its decision steps, sends the terminal marker, and exits; the weights carry
over. One run is one episode.

**The full lifecycle:** start engine → training executions in sequence, engine learning
continuously → freeze the weights → 30 evaluation executions with the frozen policy → 
offline analysis alongside the other arms.

- **Quantity is unbounded; the plateau decides.** ~1200 decision steps per 3600-second run
  (one per telemetry window) means one pass over the 40-seed pool is ~48,000 steps — small
  for RL. Cycling the pool in shuffled order fixes this: five passes is 200 episodes and
  ~240,000 steps. Do not budget a fixed run count for RL training; monitor the per-episode
  reward the engine logs and stop when it flattens. Episodes on a repeated seed do not
  repeat themselves: mobility is identical, but exploration differs and the agent's own
  actions change the placements it then observes.
- **Diversity is what the 40-seed cap actually limits**, and no amount of cycling adds a
  41st scenario. The risk is the agent specializing to those traffic patterns. It is
  manageable — the policy learns over local situations, not whole trajectories, and 40
  scenarios × ~13 users × 1200 steps generate enormous situational variety — and it is
  detectable: hold 5 of the 40 aside as a validation set (train on 35, periodically freeze
  and check on the 5). A training reward that climbs while the validation holdout stalls is
  specialization. If it bites, rebalance the pools (the LSTM may not need all 50) or
  generate additional *training* traces if the 120-seed cap is about pre-generated mobility
  files rather than anything fundamental — the evaluation 30 stay exactly as they are.
- **Exploration only in training.** Whatever exploration mechanism the engine uses is on
  during training executions and off — greedy, deterministic — in the evaluation 30. This
  is one of the two reasons training runs can never double as evaluation runs; the other is
  that an agent still updating plays each evaluation run with a different policy, which
  destroys reproducibility.
- **Engine-side recommendation:** at this data scale, an off-policy method with a replay
  buffer that persists across episodes (DQN-family, for the discrete action set of meo-plan
  item 4). Off-policy reuses every recorded transition many times; on-policy methods discard
  transitions after one use and need far more episodes than this budget wants to fund.
- **Reward:** the runtime proxy defined in meo-plan item 4, components logged per step, so
  a weak learning arm can be diagnosed offline as bad method versus misspecified reward.

---

## Evaluating the predictive models

Two evaluations, in order, neither replacing the other:

1. **Offline, on the test split inside the 50.** Standard accuracy, plus the horizon check:
   for each test prediction, was the lead time longer than pipeline delay + migration time
   (`H ≥ T + D + S + M`, ≈ 17 s in the current scenarios — meo-controller-plan § timing)?
   Report *usable* accuracy, not just raw accuracy — a model can be accurate and always too
   late. This is cheap and happens before any simulation is spent on the model.
2. **In-system, from the real Proactive evaluation runs.** No shadow mode exists or is
   needed (the reasoning is recorded in meo-plan item 1): mobility is exogenous to the
   orchestrator, so the event stream of a real run is clean ground truth, and the decision
   log joined against it scores every prediction — right host, sufficient lead — from the
   same runs that produce the system metrics.

---

## Before the long campaigns

Two prerequisites from the other plans, repeated here because this file is where the
hundreds of executions get scheduled:

- **Rebuild the release targets, `inet` included.** The INET `MessageDispatcher` patch
  (meo-plan § risk, [inet-fix.md](inet-fix.md)) is unversioned and reaches only the build
  that was rebuilt; every profile above deletes applications, and without the patch those
  runs die of stack overflow. Verify in the release build before the first campaign.
- **Every run must prove its configuration.** Results directories are named by
  configuration, and each module records the mode it actually derived (meo-plan item 6).
  With ~300 executions across four arms and three pools, a single mislabeled batch is the
  most expensive silent failure available.
