# Netplay frame phase vs sim tick (audit notes)

Port uses two independent drivers:

1. **`PortPushFrame`** ([`port/gameloop.cpp`](port/gameloop.cpp)) posts `INTR_VRETRACE` every **host present / main-loop iteration** and increments **`sFrameCount`** (see `port_get_push_frame_count()`). This runs even when VS `scVSBattleFuncUpdate` returns early at the net barrier.

2. **Net sim tick** (`sSYNetInputTick`) advances only after [`syNetPeerCheckStartBarrierReleased`](port/net/sys/netpeer.h) in [`syNetInputFuncRead`](port/net/sys/netinput.c).

**Task manager:** [`dSYTaskmanFrameCount`](port/net/sys/taskman.c) increments on the draw/update task paths; graphics bookkeeping in [`decomp/src/sys/objdisplay.c`](decomp/src/sys/objdisplay.c) compares against this counter. Any mismatch in **VI cadence** vs a peer can change **which draw branch** runs before sim ticks line up.

**RNG (global):** [`syUtils`](decomp/src/sys/utils.c) LCG state is advanced only when game code calls the random helpers — there is **no automatic** “once per `PortPushFrame`” step in `gameloop`. Risk is **indirect**: code that runs **per scheduler VI** (audio stubs, effects, scene draw) while the barrier holds **sim tick** can still consume RNG if those paths call `syUtils` or alternate PRNGs (e.g. audio `n_env.c` local seeds). **Inventory:** grep `syUtils`, `syRand`, `Random` in paths reachable from `scManagerRunLoop` / scheduler while VS battle gate is active — focus any hits **outside** `scVSBattleFuncUpdate`’s early return.

**NetSync** hashes only a subset of world state ([`netsync.c`](port/net/sys/netsync.c)); it will not catch item/particle/RNG drift that **does not** touch the sampled fighter / yakumono fields.

**Mitigations (implementation):** extend clock samples, optional conservative lead, **quantize** barrier `start_ms` to a nominal VI ms grid, apply [`syNetRollbackApplyPortSimPacing`](port/net/sys/netrollback.c) from the main loop during active VS, and **freeze taskman counter progression** while **`!syNetPeerCheckBattleExecutionReady()`** **during `nSCKindVSBattle` only**, then **resync counters at barrier release** (see [netplay_matchmaking.md](./netplay_matchmaking.md) — *Taskman phase lock during barrier*). **Adaptive rollback delay** must stay **symmetric** on wire tick labels: host broadcasts delay via **`INPUT_DELAY_SYNC`**; guests do not tune delay locally. Optional **`SSB64_NETPLAY_TASKMAN_DEBUG`** enables noisy taskman tick-wait logging (default off).

## Automatch visible sync hold (default)

**Staging → VS:** After P2P bootstrap, [`scnetmatchstaging.c`](../port/net/sc/sccommon/scnetmatchstaging.c) calls `syNetPeerStartVSSession()` then **`mnVSNetAutomatchAMFinalizeVsLoad()` immediately** so clock / bind / exec-sync wait runs under **`nSCKindVSBattle`** (taskman net-freeze + frozen sim tick).

**Host-frame gate pump:** [`PortPushFrame`](../port/gameloop.cpp) calls [`syNetPeerPumpBattleGateOnHostFrame`](../port/net/sys/netpeer.c) after `port_resume_service_threads()` when the VS session is active and execution is not ready (`syNetPeerShouldPumpBattleGateOnHostFrame`). This **duplicates** `syNetPeerUpdateBattleGate` with the taskman freeze loop and staging (if env hold-on-staging) — **safe**: extra `recvfrom` drains are idempotent. Disable with **`SSB64_NETPLAY_HOSTFRAME_GATE_PUMP=0`**.

**Presentation hold:** While `syNetPeerWantsSyncPresentHold()` (active session, `nSCKindVSBattle`, `!syNetPeerCheckBattleExecutionReady`, default **on** when env unset), `PortPushFrame` **skips idle `PresentCurrentFramebuffer`** and only sleeps ~one VI so both windows do not keep animating stale staging. Disable with **`SSB64_NETPLAY_SYNC_PRESENT_HOLD=0`**.

## Two-client automatch log checklist

1. After match, confirm **early VS** path: scene jumps to battle while logs still show clock / barrier wait (or use staging-hold env for A/B).
2. **`SSB64 NetPeer: barrier schedule`** / **clock-deadline** / **barrier release** lines appear while **`execution begin`** is still pending.
3. **`TIME_PING` / `BATTLE_START_TIME`** progression during slow disk (host-frame pump must keep UDP alive during `scVSBattleStartBattle`).
4. **`input_bind_ack`** / **`battle_exec_sync`** (if enabled) before **`execution begin`**.
5. Guest **`late=`** sane vs host after go-live.
