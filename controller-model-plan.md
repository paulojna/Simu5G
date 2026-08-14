# The Controller and the model server

Refactor 3 of three. Refactor 1 settled the Controller ↔ orchestrator boundary
([meo-controller-plan.md](meo-controller-plan.md)); refactor 2 settled the orchestrator's
internals and built the orchestrator ↔ learning-engine boundary
([meo-plan.md](meo-plan.md)). This is the last one: how the Controller talks to the model
server that produces predictions.

The models themselves — what they are, how they are trained, which seeds they may see — are
[model-plan.md](model-plan.md) and are not reopened here. What is varied around them is
[experiments.md](experiments.md).

Naming rule: descriptive names only. No letter labels.

---

## Scope

`PredictionServerClient` (`RavensControllerApp/TelemetrySinks/`) and the payloads it sends and
receives. Nothing about which model answers, and nothing about what the orchestrator does with
a prediction once it has one.

---

## What is settled

**The model server stays behind the Controller.** Moving it to the orchestrator was considered
and rejected; the reasoning is recorded so it is not re-argued.

The one real argument for moving it is that the pipeline would get shorter — Agents →
Controller → server → Controller → orchestrator loses two hops, and lead time is the quantity
the whole proactive arm is judged on. That argument fails on four counts:

- **It assumes away the claim being made.** The contribution is that acquiring information is
  not free. Shortening acquisition by architectural fiat removes part of the cost being
  characterised. The arm that answers "what would a shorter pipeline buy" is the oracle arm in
  [experiments.md](experiments.md), which is a labelled comparison rather than a silent change
  to the default.
- **It breaks the learning ablation.** Predictions-on versus predictions-off is only a clean
  comparison while the learning arm sees *the same stream the proactive arm acts on*. Two
  routes means two staleness profiles, and the ablation stops isolating the decision layer.
- **It is the wrong side of a boundary that already means something.** Models that *describe*
  future observations live behind the Controller — turning observations into statements about
  observations is what RAVENS is for. The model that *decides* lives behind the orchestrator.
- **It would rebuild buffering that already exists, in the wrong place.** The model consumes a
  per-user *sequence*; `onUserSamples` already assembles those. Moving it would make the
  orchestrator hold per-user sample history — a second observation history alongside the user
  view and the application view, which is the "third source restating the same thing" the
  placement snapshot was rejected for.

**What would reopen it:** evidence that the Controller hop is a material share of the
lead-time budget — that predictions are late because of the relay rather than because the
model's horizon is short. `latePredictions_` and the `observedAt` → `decidedAt` gap in the
decision log measure exactly that. If the relay dominates, that is a finding to report, not
necessarily an architecture to change.

---

## Known items

### 1. One request per frame, where everything else is one per window

The largest item, and the one the others are easiest to fix alongside.

`onTelemetryFrame` fires once per Agent frame, so the client posts **once per MEC host per
telemetry interval**. Ten hosts at a one-second interval over a 3600-second run is ~36,000
requests, each of them blocking the simulation. The window path next to it sends **one**
message per interval.

Two things follow, and the second is worse than the cost:

- **The model sees fragments.** Each request carries one host's slice. A user crossing between
  hosts inside a window appears in two separate requests, and the row that says so — the same
  user seen by two hosts with different `observedMEH` — is exactly the most informative one.
- Every request pays a connection setup (item 3) and a serialisation round.

**The fix is nearly free, because the buffers already exist.** `closeTelemetryWindow` holds
`windowUserSamples_`, `windowCellSamples_` and `windowReportingMEHs_` — the whole window,
attributed, already assembled for the orchestrator. The model call hangs off that instead of
off the frame hook.

`TelemetrySink` has no window-close hook today (`onUserSamples`, `onCellSamples`,
`onTelemetryFrame`, `onUserEvent`). Adding `onTelemetryWindow` is the smaller change and keeps
the client a sink; calling the client directly from `closeTelemetryWindow` is the alternative
and makes it not one. Prefer the hook.

**Consequence for the payload, and it is a breaking change:** the top-level `mecHostId` stops
making sense — a window spans hosts. It is already carried per row as `MEHId`, so the field is
dropped rather than moved, and the window bounds are added. The server must change in lockstep,
which is what item 6 is for.

### 2. A model server that fails degrades the run silently, and nothing records it

`postToServer` clears the response on any curl error; `parseResponse` returns an empty vector;
the run continues with no predictions for that frame. Nothing counts it, nothing logs it.

So a proactive run in which the server was slow, restarted, or briefly unreachable is
**indistinguishable from one in which the model had nothing to say** — and both are
indistinguishable from a model that is simply bad. That is a measurement-integrity problem in
the arm whose whole purpose is to measure a model.

Continuing is still the right behaviour, and this is where it differs from the learning engine,
which stops the run: a missing prediction degrades a proactive run to reactive, which is
survivable and even interesting, while a missing *action* corrupts the transition an agent
learns from. So: keep going, but count it, log it, and emit it as a scalar so a run's own
output shows how much of it was actually proactive.

The `CURLOPT_TIMEOUT` of 5 s belongs to the same item — it is a policy, not a constant, and it
should be a parameter with the failure count next to it.

### 3. A new connection per request

`curl_easy_init` / `curl_easy_cleanup` on every call, in `postToServer` and again in the
constructor's reset. One handle held for the client's lifetime gets keep-alive for free.

`LearningEngineClient` already does this; the two should look the same.

### 4. The request and the response speak different languages

The request is built from `forEachField`, so its keys are the CSV column names — `UEId`,
`MEHId`, `AccessPointId`. The reply is read as `Address`, `AccessPointId`,
`NextAccessPointId`, `Duration`.

One name per quantity, end to end, is the rule the shared record exists to enforce; the reply
is currently outside it. Bring the response keys into the same vocabulary as the request.

### 5. Host names derived from access point ids

`parseResponse` builds `"mecHost" + std::to_string(nextAPId)`, which silently assumes access
point N is served by mecHost N. That happens to hold in `tust_1to1` and is not a property of
anything. The `bsList` parameter is the actual mapping, and the orchestrator now reads it when
it builds the learning engine's topology.

Send and receive host names verbatim, as the engine boundary does.

### 6. Relative time in the reply, absolute everywhere else

The server answers `Duration`, a delay, and `parseResponse` converts with
`simTime() + Duration`. It works — the call blocks, so `simTime()` is still the frame's
instant — but it is the only relative time on either boundary, and it is correct by
coincidence rather than by construction.

Have the server answer an absolute `expectedAt`, matching `MigrationPrediction`, the decision
log, and the engine boundary.

### 7. Protocol version, since items 1, 4, 5 and 6 all change the wire

Four of the items above alter what crosses this boundary, and the server is developed
separately. Without a version, a mismatch shows up as a model that answers nonsense — which is
indistinguishable from a model that is bad, the same failure mode as item 2.

`LearningEngineClient::PROTOCOL_VERSION` is checked at `/reset` and the engine refuses an
episode it does not recognise. Do the same here: the Controller already calls `/reset` at
construction, so the field has a place to go and the check has a place to fail.

### 8. `/reset` failure is a warning

The constructor posts `/reset` so the server drops the previous run's per-user buffers, and
logs a warning if it fails. A run that then serves predictions partly derived from the previous
run is worse than a run that does not start: it produces plausible numbers that are wrong.

Given item 7 puts a version check in the same call, this becomes an error rather than a
warning — the one place this boundary should refuse to proceed.

---

## What is deliberately not changed

- **The blocking call.** Inference time is a real term in whether a proactive migration
  finishes before the user arrives, and letting simulated time run during it would hide the
  thing the arm measures.
- **The field list.** `UserSample` and `CellSample` are what the models are trained on; a
  renamed column is a retrained model. Item 4 changes the *response* vocabulary, not the
  request's.
- **`modelId` never reaching a decision.** It is carried for grouping runs and nothing may
  branch on it. That is what lets models be swapped without touching this boundary.

---

## Suggested order

1. **Items 3, 7 and 8** — connection reuse, the version field, and `/reset` becoming an error.
   Small, independent of the payload work, and item 7 has to exist before the wire changes or
   the first mismatch is undiagnosable.
2. **Item 2** — the failure counter and the timeout parameter. Before any campaign, because it
   is what proves a proactive run was actually proactive.
3. **Item 1** — per-window batching, with items 4, 5 and 6 folded into the same payload change.
   One breaking change, one server update, one version bump, rather than four.
4. Re-run one short proactive configuration and compare the decision log against a run from
   before the change: the same seeds should produce the same *decisions*, only fewer and
   larger requests.

Step 4 matters more than usual here. Item 1 changes what the model sees — a window rather than
a host's slice — so predictions may legitimately differ, and a plan that cannot tell "the model
now sees more" from "the payload broke" will not be able to say which happened.

---

## Risk

**The server must move in lockstep.** Unlike the other two refactors, both ends of this one are
not in this repository. Item 7 is what makes a lockstep failure loud instead of silent, which
is why it comes before the payload change rather than with it.

**Any model trained before item 1 was trained on frames.** Batching per window does not change
the per-user sequences the model consumes, so a trained model should carry over — but that is a
belief, not a fact, until a run confirms it. Check it on the offline test split before spending
simulation time on it.
