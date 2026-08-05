# Handoff: MEC app teardown crash in the T2 migration path

Written for a fresh session with no prior context. Two tasks, independent. **Task 1** is the
real one; **Task 2** is a small specified change at the end.

---

## Task 1 — a segfault during MEC app migration

### Symptom

Running `T2_NotifyOnDataChange` for 300 s, the simulation dies around **t = 230 s** with a
SIGSEGV. The last lines before it:

```
NEW MIGRATION STARTED WITH CONTEXT ID 67 FOR UE acr:10.0.23.187
[MigrateOnChange t=230] UE=acr:10.0.31.37 lastMEH='' newMEH='mecHost5'
Stack trace (most recent call last):
#31 libINET  inet::MessageDispatcher::arrived(cMessage*, cGate*, SendOptions const&, SimTime)
#30 liboppsim omnetpp::cGate::deliver(cMessage*, SendOptions const&, SimTime)
...  (this pair repeats ~13 times) ...
#5  libINET  inet::MessageDispatcher::arrived(...)
#4  libINET  inet::MessageDispatcher::handlePacket(inet::Packet*, cGate*)
#3  liboppsim omnetpp::cModule::gate(char const*, int)          <-- faults here
#2  libsystem_platform _sigtramp
```

Two readings of that trace, and **they need different fixes**, so establish which one first:

- **Dangling module.** `cModule::gate()` is called on a module that has been deleted. The
  dispatcher chain would then be a normal depth and the fault is a use-after-free.
- **Stack overflow.** `backward` prints only the last 32 frames, so the ~13 visible
  `arrived → deliver` pairs may be the tail of thousands — a packet looping between
  dispatchers. The fault would then land in whatever function next touches the stack, which
  happens to be `cModule::gate()`.

Distinguishing them is the first concrete task. See *How to investigate*.

### Critical fact: it is machine-dependent

**The same code, config and seed ran to completion on a different machine.** That is the most
informative thing known about this bug, and it rules the debugging approach:

- The simulation is deterministic given code + config + seeds, so a *simulation-level*
  divergence should not happen between machines. Something outside the model is varying.
- Two candidates, both plausible, both pre-existing:
  1. **`std::unordered_map` iteration order.** `RavensControllerApp::handleSelfMessage`
     (the `sendSnapshot` branch) drains `userUpdates`, an `unordered_map<std::string, ...>`,
     into a vector sent to the MEO. Hash order for `std::string` keys differs across libc++
     versions, so **the order in which migrations are requested differs between machines.** If
     the underlying bug is order-sensitive, that alone explains it. `userStateMap` is walked
     the same way in `expirePendingExits()`, which affects the order of `onUserExit` calls.
  2. **Uninitialised memory** in the VIM / orchestrator — for example a `serviceIndex` used to
     pick gates to `disconnect()`. Classic "works on one machine".

Note that (1) also means **the training corpus is not byte-reproducible across machines**,
which is worth recording separately regardless of this bug.

### What has already been ruled out

- **It is not the RAVENS Controller changes.** In `T2` the Controller runs in event-only mode:
  the Agent sends no telemetry frames, so the telemetry-driven code paths never execute. The
  Controller's externally visible behaviour in that profile is unchanged.
- **It is not the earlier `removeInactiveUsers` bug.** There *was* a separate crash at ~60 s
  caused by a liveness timeout deleting live users and the MEO tearing down their apps. That is
  fixed (users now leave the Controller's model only via the event channel). Fixing it is what
  let the run survive to 230 s and reach *this* crash — which is therefore probably
  pre-existing and was simply masked.

**Cheap confirmation, worth doing first:** check out the commit before the RAVENS Step 5/6 work
and run `T2` on the machine that crashes. If it also dies around 230 s, the bug is pre-existing
and no RAVENS code needs looking at.

### The call chain

Reactive migration in this profile:

1. `src/nodes/mec/MECOrchestrator/reactionOnUpdateStrategies/MigrateOnChange.cc:30`
   `reactOnUpdate(const UserMEHUpdate&)` — three cases:
   - `newMEHId == ""` → `api_->removeAppFromSystem(...)` **with no check that the UE actually
     left** (`:38-45`)
   - both set and different → `api_->migrateApp(...)` (`:47`)
   - `lastMEHId == ""` → `api_->checkIfMigrationIsNeeded(...)` (`:70`)

2. `MecAppMigrationManager::performMigration` — creates the new instance on the target host,
   registers it, then tells the UE to switch:
   - `.../MecAppMigrationManager.cc:485` `sendMehChangeRequest`, sent **delayed by
     `migrationTime_`**
   - `:458` the request goes into `standByList_`, `:462` a timeout is scheduled

3. `MecAppMigrationManager::completeMigration` (`:213`) — on the UE's ack, terminates the *old*
   instance via `mecAppLifecycleManager_->stopApplication(standBy.contextId)` (`:238`).

4. `VirtualisationInfrastructureManager::terminateMEApp(DeleteAppMessage*)`
   (`src/nodes/mec/VirtualisationInfrastructureManager/VirtualisationInfrastructureManager.cc:416`)
   — **this is where the suspicious code is.**

### Prime suspect: `terminateMEApp`

```cpp
:427   mecAppMap[key].meAppModule->callFinish();
:428   mecAppMap[key].meAppModule->deleteModule();      // module destroyed
...
:438   int index = mecAppMap[key].meAppGateIndex;
:439   int serviceIndex = mecAppMap[key].serviceIndex;
...
:444   virtualisationInfr->gate("meAppOut", index)->getPreviousGate()->disconnect();
:445   virtualisationInfr->gate("meAppIn", index)->disconnect();
:447   if(serviceIndex >= 0) {
:450       (meServices.at(serviceIndex))->gate("meAppOut", index)->disconnect();
:451       (meServices.at(serviceIndex))->gate("meAppIn", index)->disconnect();
:453       mecPlatform->gate("meAppOut", index)->disconnect();
:454       mecPlatform->gate("meAppIn", index)->disconnect();
       }
:462   mecAppMap.erase(ueAppID);
:464   freeGates.push_back(index);                      // index recycled for the next app
```

Three hazards, in order of suspicion:

1. **Gate indices are recycled** (`:464`) with nothing draining packets already in flight
   toward the old index. A later app allocated the same index inherits anything still routed
   there.
2. **Nothing checks whether a service still holds a connection bound to that index.** See
   below.
3. `deleteModule()` at `:428` happens *before* the gates are disconnected at `:444`, and the
   map entry is read at `:438-439` after the module is gone. Reading `meAppGateIndex` from the
   struct is safe (the struct outlives the module), but the ordering deserves checking against
   what OMNeT++ guarantees about deleting a module with connected gates.

### Second suspect: services never clean up their connections

`src/nodes/mec/MECPlatform/MECServices/MECServiceBase/SocketManager.cc:103`

```cpp
void SocketManager::peerClosed()
{
    ...
    //service->removeConnection(this); //sock->close(); // it crashes when mec app is deleted  with ->deleteModule FIXME
```

The cleanup is **deliberately disabled with a note that it crashes on exactly this path.** So
every deleted MEC app leaves a stale `SocketManager` module in the service's `threadSet` and
`socketMap` (`MecServiceBase.cc:557` `closeConnection`, `:568` `removeConnection`). Those
accumulate over a run, each still wired to a gate index that may since have been reassigned.

This is a standing acknowledgement that teardown is broken. Whoever picks this up should treat
that FIXME as part of the same bug, not a separate one.

### How to investigate

1. **Reproduce.** From `simulations/NR/mec/ravensEnabledScenarios/tust_1to1/`:
   ```
   ./run_test.sh T2_NotifyOnDataChange
   ```
   Config is in `omnetpp.ini` (`[Config T2_NotifyOnDataChange]`, extends `Ravens_Test_Base`:
   `sim-time-limit = 300s`, `repeat = 1`, `seed-set = 0`).

2. **Decide between "dangling module" and "stack overflow".** Build `MODE=debug` and run under
   `lldb`. Then:
   - `bt` — if the dispatcher recursion is thousands of frames deep, it is a routing loop.
     If it is ~15 frames, it is a use-after-free.
   - `frame select` the `cModule::gate` frame and inspect `this`. A garbage or freed pointer
     confirms the dangling-module reading.
   - The packet being delivered names its intended destination; find which gate index and which
     module it is aimed at, and cross-reference against `freeGates` / `mecAppMap`.

3. **If it is a use-after-free**, run under ASan (`-fsanitize=address`) — it will name the
   freed object and the deleting stack directly, which is far faster than reasoning about it.

4. **Confirm the order sensitivity.** Temporarily make the MEO's update order deterministic —
   sort `userUpdates` by address before draining it in `RavensControllerApp::handleSelfMessage`
   — and see whether the crash moves or disappears. If it does, the bug is order-dependent,
   which both explains the machine difference and gives a reliable reproduction knob.

### Candidate fixes (decide after diagnosis, not before)

- **Quarantine gate indices** instead of recycling immediately: hold a freed index unused for
  some interval, or never reuse it. Cheapest way to test the recycling hypothesis.
- **Clean up service connections on app deletion** — the disabled `removeConnection` path.
  Needs to happen *before* `deleteModule()`, which is probably why it crashed when tried.
- **Make teardown tolerate in-flight packets** — drain or drop packets addressed to the app
  before deleting the module.
- **Make the MEO refuse to tear down a UE it still believes is placed.** This does not fix the
  crash but removes a whole class of triggers, and is already a planned item (see
  `RAVENS_PLAN.md`, MEO round, item 2): `removeAppFromSystem` is currently executed blind on any
  update with an empty `newMEHId`.

### Scope

- **Do not modify anything under `src/apps/mec/RavensApps/`** unless the diagnosis actually
  lands there. That is the RAVENS Controller/Agent work, unrelated and recently changed.
- `RAVENS_PLAN.md` at the repo root is the working plan. The MEO round is described there and
  is not yet broken into steps; if this fix grows into MEO restructuring, record it there rather
  than improvising.
- The affected code (`VirtualisationInfrastructureManager`, `MecServiceBase`, `SocketManager`)
  is largely inherited Simu5G, not project-authored. Fixes there are legitimate but should be
  minimal and commented, since they diverge from upstream.

---

## Task 2 — the Agent does RNIS work it never uses in event-only mode

Small, fully specified, unrelated to the crash.

**Problem.** In `EVENT_ONLY_MODE` the Agent sends no telemetry frames, but
`RavensAgentApp::handleRNISMessage` still runs in full on every notification (once a second, per
host): it parses the notification JSON, builds a cell record that is then discarded, and writes
per-UE radio values into the `users` map that are only ever read when a sample is copied for a
telemetry frame — which that mode never sends. All of it is dead work, and the JSON parse is the
expensive part on a busy cell.

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

`agentMode_` starts at `FULL_MODE` and is corrected by the handshake ACK, so a few notifications
early in the run still do the work. Harmless.

**Related, deliberately not part of this.** The L2Meas subscription itself is created regardless
of mode, so the RNIS builds and sends a notification every second for a profile that reads none
of it. Skipping the subscription entirely means either deferring it until after the handshake or
unsubscribing once the mode is known — `sendL2MeasSub` is scheduled from the RNIS socket's
`established()`, while the mode arrives with `INFRASTRUCTURE_DETAILS_ACK`, and there is no
guarantee of ordering between them. That is a sequencing change with real risk; treat it as its
own task if wanted.

Note this does not affect any published signaling number: those are scoped to the
Agent → Controller path, and this traffic is internal to the MEC host. It does mean the claim
that event-only mode does "no telemetry work" is currently looser than it reads.
