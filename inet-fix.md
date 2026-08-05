# Local modification to INET — `MessageDispatcher` disconnected-gate guard

**This patch lives outside version control.** `inet/` is not a git repository in this
setup, so nothing tracks this change. Reinstalling, updating, or re-cloning INET silently
reverts it, and the only symptom is that long MEC runs start dying with a stack overflow
again. This file exists so the change can be reapplied without redoing the investigation.

- **Applies to:** `inet-4.5.4-0a1d409733` (contents of `inet/Version`)
- **File:** `inet/src/inet/common/MessageDispatcher.{h,cc}`
- **Depends on nothing.** It is self-contained and can be applied before or after the
  Simu5G-side changes described at the end.

---

## What goes wrong without it

### Symptom

A simulation that deletes MEC apps at runtime dies with `EXC_BAD_ACCESS` in
`___chkstk_darwin` — macOS's stack-probe function. That is a **stack overflow**, not a bad
pointer, and the distinction matters: it looks like a use-after-free in a truncated
backtrace, and chasing it as one wastes a lot of time.

`bt` shows the same two frames repeated for as deep as you care to print, with **identical
argument values every time** — same module pointer, same message pointer, same gate:

```
frame #0:   libsystem_pthread.dylib`___chkstk_darwin + 60
frame #1:   inet::MessageDispatcher::handleMessage(...)          at MessageDispatcher.cc:206
frame #2:   inet::MessageDispatcher::arrived(this=0x...838, message=0x...740, inGate=0x...170) at MessageDispatcher.cc:49
frame #3:   omnetpp::cGate::deliver(this=0x...170, msg=0x...740)  at cgate.cc:481
frame #4:   inet::MessageDispatcher::arrived(this=0x...838, message=0x...740, inGate=0x...170) at MessageDispatcher.cc:50
frame #5:   omnetpp::cGate::deliver(this=0x...170, msg=0x...740)  at cgate.cc:481
   ... repeats ...
```

The giveaway is that `cGate::deliver`'s `this` equals `arrived`'s `inGate`: the dispatcher
is delivering into the very gate the message keeps arriving on.

### Mechanism

Two behaviours combine. Neither is a bug on its own; the combination is only reachable if
modules are deleted while the simulation runs.

**1. OMNeT++ delivers a disconnected gate's message back to its own module.**
`omnetpp-6.1/src/sim/cgate.cc`, `cGate::deliver`:

```cpp
bool cGate::deliver(cMessage *msg, const SendOptions& options, simtime_t t)
{
    if (!nextGate) {
        getOwnerModule()->arrived(msg, this, options, t);   // <-- hands it back
        return true;
    }
    ...
```

Sending out a gate that connects to nothing is not an error. The message comes straight
back to the sender.

**2. INET's `MessageDispatcher` never forgets a route.**
`MessageDispatcher::handlePacket` resolves a socket id to an out gate:

```cpp
auto it = socketIdToGateIndex.find(socketId);
if (it != socketIdToGateIndex.end()) {
    auto outGate = gate("out", it->second);
    return outGate;
}
```

`socketIdToGateIndex` is written in exactly one place — `handleMessage`, on a `SocketReq`
tag — and is **never erased from, anywhere in the class.** Same for
`interfaceIdToGateIndex`, `serviceToGateIndex` and `protocolToGateIndex`. Upstream assumes
a static topology where a registered module never goes away.

**Put together:** delete a module behind a dispatcher, and its gate is disconnected while
the routing tables still resolve to it. The next message for that socket is sent to the
dead gate, comes back, is resolved to the same dead gate, comes back… until the stack is
exhausted. Note that every *other* branch in `handlePacket` throws a clean `cRuntimeError`
when it cannot resolve a route; the socket branch is the one case that finds a stale answer
instead of failing loudly, which is why this presents as a hang-then-crash rather than an
error message.

### How it was reached in this project

Simu5G's `VirtualisationInfrastructureManager::terminateMEApp` deletes a MEC app module,
disconnects its gates, and pushes the gate index back onto `freeGates` for reuse. Any
message still addressed to that app afterwards — a UE data packet, or a `TCP_I_CLOSED`
confirmation from the app's own socket teardown — hits the loop.

This went unnoticed for years because MEC apps were almost never actually deleted (see
*Related Simu5G changes* below). Once they were, the crash appeared within a few minutes of
simulated time.

---

## The change

### 1. `inet/src/inet/common/MessageDispatcher.h`

Add the counter to the protected data members, immediately after `registeringAny`:

```cpp
    const Protocol *registeringProtocol = nullptr;
    bool registeringAny = false;

    // LOCAL MODIFICATION (not upstream INET), see arrived(): counts messages discarded
    // because they resolved to a gate with nothing on the far side. Should be 0 in a
    // network whose modules all outlive the simulation; a non-zero value is expected
    // only where modules are created and deleted at runtime, as MEC apps are.
    long numDiscardedToDisconnectedGate = 0;
```

Add the `finish()` declaration alongside the existing overrides:

```cpp
  protected:
    virtual void initialize(int stage) override;
    virtual void finish() override;                          // <-- added
    virtual void arrived(cMessage *message, cGate *gate, const SendOptions& options, simtime_t time) override;
    virtual cGate *handlePacket(Packet *packet, cGate *inGate);
    virtual cGate *handleMessage(Message *request, cGate *inGate);
```

### 2. `inet/src/inet/common/MessageDispatcher.cc`

In `arrived()`, insert the guard between resolving `outGate` and delivering to it. The
`outGate->deliver(...)` line is the one that must become unreachable for a dead gate.

**Before:**

```cpp
    else
        outGate = handleMessage(check_and_cast<Message *>(message), inGate);
    outGate->deliver(message, options, time);
#ifdef INET_WITH_QUEUEING
    updateDisplayString();
#endif // #ifdef INET_WITH_QUEUEING
}
```

**After:**

```cpp
    else
        outGate = handleMessage(check_and_cast<Message *>(message), inGate);

    // LOCAL MODIFICATION (not upstream INET).
    //
    // A gate with nothing on the far side is not a destination. cGate::deliver() treats
    // a null nextGate as "deliver to my own owner", so handing the message to it calls
    // this very method again, with the same message, which resolves to the same gate --
    // an unbounded recursion that ends in a stack overflow rather than an error.
    //
    // It is reachable whenever a module behind this dispatcher is deleted at runtime.
    // The module's gate is disconnected, but the tables above still resolve to it:
    // socketIdToGateIndex and its siblings are insert-only, with no removal path
    // anywhere in this class, because upstream assumes a static topology.
    //
    // Discarding is the correct outcome, not a workaround -- the addressee no longer
    // exists, and these are overwhelmingly teardown confirmations (TCP_I_CLOSED and
    // friends) that nothing can act on. It is counted rather than silent so that a
    // network which should never hit this can be shown not to.
    if (outGate->getNextGate() == nullptr) {
        EV_WARN << "Discarding message addressed to a disconnected gate: "
                << message->getFullName() << EV_FIELD(inGate) << EV_FIELD(outGate) << EV_ENDL;
        numDiscardedToDisconnectedGate++;
        take(message);
        delete message;
        return;
    }

    outGate->deliver(message, options, time);
#ifdef INET_WITH_QUEUEING
    updateDisplayString();
#endif // #ifdef INET_WITH_QUEUEING
}

void MessageDispatcher::finish()
{
#ifdef INET_WITH_QUEUEING
    PacketProcessorBase::finish();
#endif // #ifdef INET_WITH_QUEUEING
    // See arrived(). Recorded unconditionally so a zero is evidence rather than absence.
    recordScalar("discardedToDisconnectedGate", numDiscardedToDisconnectedGate);
}
```

### Detail notes for whoever reapplies this

- **`take(message)` before `delete` is required**, not decoration. The dispatcher does not
  own an arriving message. Deleting without taking ownership produces OMNeT++'s
  *"deleting an object it doesn't own"* warning on every discard. `take()` is the idiom
  this same file already uses in `pushPacket`, `pushPacketStart` and `pushPacketEnd`.
- **The guard must sit after `handlePacketProcessed(packet)`**, which the queueing build
  calls inside the `isPacket()` branch. Placing it earlier would skip that accounting.
- **The early `return` skips `updateDisplayString()`.** Intentional and harmless — nothing
  was delivered.
- **`getNextGate()`, not `getPathEndGate()`.** The loop only occurs when the *immediate*
  out gate is disconnected, because that is the case where `cGate::deliver` calls back into
  this same module. A chain ending in a disconnected gate further along delivers to a
  different module and fails differently.
- **`PacketProcessorBase::finish()` is called only under `INET_WITH_QUEUEING`**, matching
  how `initialize()` in this file is written. In the non-queueing build the base is
  `cSimpleModule`, whose `finish()` is empty.

---

## Rebuilding

**INET must be rebuilt — a Simu5G build will not pick this up.**

```
cd inet && make MODE=debug      # and/or MODE=release
cd ../Simu5G && make MODE=debug
```

Both variants need it if you use both. A debug-only rebuild leaves the release library
still carrying the bug, which is a confusing state to debug in.

---

## Verifying it is present and working

**Is the patch applied?**

```
grep -n "numDiscardedToDisconnectedGate" inet/src/inet/common/MessageDispatcher.cc
```

**Is it doing anything?** Every `MessageDispatcher` records a scalar, so any results file
shows it:

```
grep discardedToDisconnectedGate <results>.sca
```

A **zero** everywhere means no module was deleted behind a dispatcher, or all their routes
happened to be re-used before anything arrived. A **non-zero** value is expected in
scenarios with runtime MEC app teardown and is not itself a fault.

**What is being discarded** matters more than how many. With `cmdenv-express-mode = false`
and `cmdenv-log-level = warn`, each discard logs the message name:

- `CLOSED`, `closed`, `PEER_CLOSED` — teardown confirmations for an app that no longer
  exists. Correct to drop; nothing further to do.
- `RequestResponseAppPacket` or other application data — **live UE traffic being dropped.**
  Not a crash, but it means an app was deleted while its UE was still using it. That is an
  orchestration correctness question, not an INET one.

---

## Related Simu5G changes (context, not part of this patch)

Committed on branch `eRavens/mec-app-teardown`. They explain why this INET bug only
surfaced recently, and the guard should be read together with them.

1. **`fix: release MEC app sockets before deleting the module`** — `MecAppBase` gains
   `releaseSockets()`, called from `terminateMEApp` *before* `callFinish()`/`deleteModule()`
   while the app's gates are still connected. It calls `destroy()` (not `close()`, which
   needs a handshake the dying module cannot complete) on every TCP socket in `sockets_`;
   `MECPerfApp` overrides it to also destroy `ueAppSocket_`, its UE-facing UDP socket.
   Without this, `Udp` keeps a socket registered for a module that no longer exists and
   keeps tagging arriving UE packets with its socket id.

   *Other MEC apps with a UE-facing UDP socket* — `MECResponseApp`, `MECWarningAlertApp`,
   `MecRnisTestApp`, `MecRTVideoStreamingReceiver` — still need the same override if they
   are ever used in a scenario with runtime teardown.

2. **`fix: destroy the migrated-from MEC app instead of leaking it`** —
   `MecAppMigrationManager::completeMigration` called
   `stopApplication(standBy.contextId)` with a contextId that `performMigration` had
   **already unregistered** when it registered the new instance. The lookup therefore always
   missed, the old instance was never destroyed, and the failure was only logged. Replaced
   with `terminateOldInstance()`, which goes to the old host's `MecPlatformManager` directly
   using `standBy.oldMecpm` and `standBy.mecUeAppID` — two fields captured for exactly this
   and previously unused.

   **This is why app deletion was so rare before.** Migrations never deleted anything, so
   the only path that reached `deleteModule()` was `MecOrchestrator::removeAppFromSystem`,
   driven by RAVENS EXIT events — which themselves never fired until an unrelated bug in
   `RavensAgentApp` departure detection was fixed. Fixing that exposed the crash; fixing the
   migration leak made it reproduce roughly three times sooner.

### Ordering caution

Change 1 makes teardown *more* correct but does not on its own stop the crash — it changes
which message loops, from a UE data packet to the `TCP_I_CLOSED` confirmation of the
teardown itself (`TcpConnectionBase.cc:643-645`). Every socket-teardown path produces some
confirmation, so suppressing them individually is a losing game. **The INET guard is what
actually closes the failure mode**; changes 1 and 2 make the teardown correct underneath it.

---

## Known open item

A *drain window* — deferring `deleteModule()` and gate disconnection by a short interval so
that teardown confirmations land while the app still exists — was considered and deferred.
It is the more principled repair, but it costs several hours (deferred deletion in the VIM,
a new NED parameter, split resource/gate-index accounting, and a module that is alive but
socketless for the interval), and the interval itself is a guess with no guarantee of
covering every confirmation. The guard is unconditional and needs no tuning.

Revisit only if the discard log shows application data rather than teardown confirmations.
The decision is tracked in [meo-plan.md](meo-plan.md), under *What changed underneath*.

---

## Related documents

- **[ravens-plan.md](ravens-plan.md)** — the RAVENS working plan and its settled decisions.
- **[meo-plan.md](meo-plan.md)** — the MEO round, including the teardown-policy question this
  patch makes survivable but does not answer.
