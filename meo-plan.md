# MEO round

The last work above the regeneration gate. See [ravens-plan.md](ravens-plan.md) for the
settled decisions this builds on and for the gate itself.

Identified, not yet broken into steps. Same naming rule applies: descriptive names only.

---

## Open decision: what `USERS_UPDATE` carries

The Controller accumulates *transitions* in a map keyed by UE address and drains it every
snapshot. Two transitions for the same UE inside one window overwrite each other: a UE going
A→B and then B→C is reported only as `{from: B, to: C}`, and the MEO is never told about A→B.

This is benign today but only by luck. `MecAppMigrationManager::migrateApp` finds the app by
UE address in its own registry and uses `oldMEHId` only for log lines and the result struct,
so the wrong value is ignored. The field is still wrong and the logs still are.

**Option A — keep transitions, fix the composition.** Merge rather than overwrite, so `{A→B}`
then `{B→C}` becomes `{A→C}`, and a UE ending where it started drops out. Correct, small, but
it is an invariant that every future edit has to respect.

**Option C — send placements.** The Controller sends the current placement of every UE it
knows about (`{address, currentMEH}`) every snapshot; the MEO diffs at ingress against its own
map and synthesises entry / handover / exit. The strategies keep receiving the same
`UserMEHUpdate` they receive today — it is just derived at the MEO's front door instead of
accumulated at the Controller's back door.

**Recommendation: Option C.** A placement is idempotent, so last-write-wins becomes the
correct behaviour rather than a bug — the problem stops being expressible instead of being
fixed. Three further reasons:

- **It is free here.** Full state every snapshot would be real traffic on a real link, but the
  Controller↔MEO link is deliberately unmodelled because the two are co-located. No cost in
  the model, and defensible in the write-up.
- **The MEO becomes self-correcting.** Its view currently can drift from the Controller's and
  never recover. Periodic full placements re-converge every snapshot, which also handles a
  false-positive prediction — a migration fired for a UE that did not move is corrected at the
  next snapshot, with no separate mechanism needed.
- **It mirrors a pattern already in the system.** The Location Service sends the Agent a full
  list every second and absence means gone. The same authoritative-full-state semantics at the
  Controller↔MEO boundary means one idea applied at both boundaries rather than two
  conventions.

Cost: a diff loop at MEO ingress, roughly 40 lines. `MeoOutput` loses its transition tracking
and `emitUserUpdate` entirely and becomes the placement publisher; its presence in the output
list still marks "this profile talks to the MEO".

---

## Known items

### 1. `USERS_UPDATE` semantics

The decision above, plus removal of the coalescing.

### 2. Teardown must tolerate the UE still being present

`MigrateOnPrediction::handleScheduledEvent` calls `removeAppFromSystem` on a predicted exit
with no reactive confirmation. `MigrateOnChange::reactOnUpdate` does the same on any update
with an empty `newMEHId` (`MigrateOnChange.cc:38-45`). A false exit deletes an app that is
never recreated, because app creation is driven by the UE requesting one and the UE never
re-requests.

**Rewritten after the August 2026 teardown investigation.** This item previously read *"the
MEO should verify the UE is absent from its own placement view before deleting"*. **That
guard would not have caught the bug it was written for**, and the reasoning behind it
conflates two different facts:

- On a confirmed EXIT the MEO's placement view *agrees* the UE is gone. The check passes and
  the delete proceeds.
- What "gone" means at that point is **gone from RAVENS' observation**, not gone from the
  simulation. The UE module can still exist, still be attached, and still be sending to its
  MEC app every 100 ms — `UEPerfApp::sendRequest` reschedules unconditionally
  (`UEPerfApp.cc:415`) until `stopTime`.

So the real requirement is the stronger one this item's title now states. Two things follow:

- **The UE is never told its app was removed.** `MecOrchestrator::removeAppFromSystem` sets
  `requestId = 0` specifically so the UALCMP sends nothing back (`MecOrchestrator.cc:458-459`).
  A UE whose app is deleted keeps transmitting into a void forever, and nothing ever recreates
  the app. Whatever the exit policy ends up being, this silence is a design choice that should
  be made deliberately rather than inherited.
- **Deleting a MEC app is now genuinely safe at the transport layer**, which it was not
  before — see *What changed underneath* below. That removes the crash, not the correctness
  problem: an app deleted under a live UE is still wrong, it just no longer takes the
  simulation down with it.

Open question for this round: should a confirmed EXIT delete the application at all, or only
mark it for reclamation with a much longer, separately-reasoned timeout? Decide from the
discard log described below.

### 3. MEO execution log

Extend the migration log (executed / stale / queued / failed / ignored, plus reactive
fallbacks) and write it into the Controller's `run_<N>` directory so it joins against the
lifecycle CSV.

Now also worth logging, because both became observable during the teardown work: whether a
migration's old instance was actually destroyed, and whether a delete was executed against a
UE the MEO still believed to be placed.

### 4. Duplicated type constants

`USERS_UPDATE` and `MIGRATION_PLAN` are `#define`d separately in `RavensControllerApp.cc` and
`MecOrchestrator.cc`, with a `TODO` already on the second copy. Move to a shared header.

### 5. Dead return path

The Controller declares an `inGate` and the MEO a `toRavensController` gate, but
`handleMessageWhenUp` has no branch for it, so anything sent back would fall through to the
unknown-message case and be dropped. Either wire it or remove it; it must not stay as a gate
that silently discards.

### 6. Prediction sub-profiles

Trust-the-prediction versus correct-with-reactive-fallback, as a strategy flag on the MEO
side. Controller output is identical in both.

### 7. Unread MEO-side state

Noted while working through Step 6 and still true: `getMecHostIdFromAccessPointId` is declared
but never defined or called, and with `updateMehStateMap` gone `mehStateMap` is written but
never read, since that undefined function was its only intended reader. Left alone because
this round plausibly wants it — recorded so it is not rediscovered as a surprise.

---

## What changed underneath (August 2026)

Three fixes landed on `eRavens/mec-app-teardown` before this round started. They matter here
because they change what the MEO's decisions actually *do*.

**MEC apps were almost never deleted.** Two independent bugs meant `deleteModule()` was
effectively unreachable:

- Agent EXIT detection never fired at all (`aa253997`), so `removeAppFromSystem` was dead code
  in the reaction profile.
- `MecAppMigrationManager::completeMigration` called `stopApplication(standBy.contextId)` with
  a contextId that `performMigration` had **already unregistered** when it registered the new
  instance. The lookup always missed, the old instance was never destroyed, and the failure was
  only logged. Fixed in `f4550e46` via `terminateOldInstance()`, which goes to the old host's
  `MecPlatformManager` directly using `standBy.oldMecpm` and `standBy.mecUeAppID`.

**So every migration used to leak a live app on the source host, and no exit ever removed
one.** Both are now fixed, which means this round's decisions about when to delete finally
have real consequences.

**Deleting an app used to crash the simulation**, in a way that looked like a use-after-free
and was actually an unbounded recursion between INET's `MessageDispatcher` and a disconnected
gate. Closed by `34e244ee` (release sockets before deleting the module) and a local INET
modification documented in [inet-fix.md](inet-fix.md). **That INET change is unversioned and
will be lost on any INET reinstall.**

**Deferred:** a *drain window* — postponing `deleteModule()` and gate disconnection so that
teardown confirmations land while the app still exists. More principled than the guard, but it
costs several hours (deferred deletion in the VIM, a new NED parameter, split resource and
gate-index accounting, and a module that is alive but socketless for the interval) and the
interval is a guess with no guarantee of covering every confirmation. The guard is
unconditional and needs no tuning.

**The trigger for revisiting it**, and a useful input to item 2: every `MessageDispatcher` now
records a `discardedToDisconnectedGate` scalar. If the discards are teardown confirmations
(`CLOSED`, `closed`, `PEER_CLOSED`), nothing further is needed. If application data
(`RequestResponseAppPacket`) appears, apps are being deleted under live UEs and item 2 is
urgent rather than tidy.
