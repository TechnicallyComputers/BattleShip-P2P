# Netplay Architecture

## Purpose

`src/sys/netinput.c` and `src/sys/netinput.h` prepare the VS Mode controller path for rollback netcode. The module owns deterministic per-player input samples for each VS simulation tick, records those samples in bounded history buffers, and republishes the resolved input through the existing `SYController` globals that battle code already consumes.

Companion handoff notes for future agent sessions live in `docs/netcode_agent_rules.md`.

This keeps the first netplay boundary at the controller layer:

- local hardware input still comes from `src/sys/controller.c`
- battle simulation still reads `gSYControllerDevices`
- netplay-only input policy lives in `src/sys/netinput.c`
- debug replay file I/O lives in `src/sys/netreplay.c`
- debug UDP P2P transport and match bootstrap live in `src/sys/netpeer.c`
- narrow gameplay-state hashing for diagnostics lives in `src/sys/netsync.c`
- **rollback (in implementation):** deterministic gameplay snapshots, rollback coordinator (misprediction → restore → resim), and fixed session **SIM_HZ** decoupled from monitor refresh — see [Rollback netcode (in implementation)](#rollback-netcode-in-implementation)

## Current Integration

VS battle uses `syNetInputFuncRead` as its controller callback. 1P Game, Training Mode, Bonus 1 Practice, Bonus 2 Practice, menus, and other non-VS scenes remain on `syControllerFuncRead`.

The VS update order is:

```text
VS scene start
  -> syNetInputStartVSSession()
  -> syNetReplayStartVSSession()
  -> syNetPeerStartVSSession()
  -> bootstrap P2P sessions enable the execution gate
  -> netinput tick starts at 0

taskman game tick during VS
  -> scene controller callback
  -> syNetInputFuncRead()
  -> syControllerFuncRead()
  -> resolve one SYNetInputFrame per player for the current tick
  -> publish resolved frames into gSYControllerDevices
  -> advance netinput tick once the optional P2P start barrier is released
  -> scene_update()
  -> syNetPeerUpdateBattleGate()
  -> return early if bootstrap P2P execution is not ready
  -> syNetReplayUpdate()
  -> syNetPeerUpdate()
  -> fighter input derivation
```

`syNetInputGetTick()` returns a VS-local tick counter reset by `syNetInputStartVSSession()`. `dSYTaskmanUpdateCount` remains part of the engine, but netplay input history is keyed by match-local time so rematches and scene transitions do not reuse stale global tick assumptions.

The P2P start barrier and VS execution gate are debug-only and only active when both `SSB64_NETPLAY=1` and `SSB64_NETPLAY_BOOTSTRAP=1` are set. Local VS, replay playback/recording, and manual P2P input injection without bootstrap continue advancing netinput and VS updates immediately.

## Canonical Input Frame

`SYNetInputFrame` is the deterministic input record used by netplay:

```c
typedef struct SYNetInputFrame
{
    u32 tick;
    u16 buttons;
    s8 stick_x;
    s8 stick_y;
    u8 source;
    ub8 is_predicted;
    ub8 is_valid;

} SYNetInputFrame;
```

The canonical frame stores held buttons and analog stick position only. `SYController.button_tap`, `button_release`, and `button_update` are derived when publishing back into the legacy controller globals.

## Input Sources

Each controller slot has a `SYNetInputSource`:

| Source | Meaning |
| ------ | ------- |
| `nSYNetInputSourceLocal` | Read the current local `gSYControllerDevices[player]` state after `syControllerFuncRead()` updates it. |
| `nSYNetInputSourceRemoteConfirmed` | Use a remote input sample already delivered for this tick. |
| `nSYNetInputSourceRemotePredicted` | Use confirmed remote input if present; otherwise predict from the last confirmed remote input, or neutral if none exists. |
| `nSYNetInputSourceSaved` | Use a saved input sample for replay or deterministic validation. |

Slots default to local input. This means routing VS battle through `syNetInputFuncRead()` should preserve local controller behavior until a caller explicitly changes a VS slot source.

## History Buffers

`netinput.c` keeps three bounded ring buffers per player:

- `sSYNetInputHistory` — resolved and published inputs used by simulation
- `sSYNetInputRemoteHistory` — confirmed remote inputs staged by tick
- `sSYNetInputSavedHistory` — saved/replay inputs staged by tick

All buffers use `SYNETINPUT_HISTORY_LENGTH` and index by `tick % SYNETINPUT_HISTORY_LENGTH`. Reads validate both `is_valid` and exact `tick` to reject stale ring entries.

For full-match debug replay files, `netinput.c` also keeps a separate replay frame stream capped by `SYNETINPUT_REPLAY_MAX_FRAMES`. This avoids treating the 720-frame rollback ring as permanent replay storage.

## Publishing To SYController

`syNetInputPublishFrame()` converts a canonical frame back into the existing `SYController` shape:

- `button_hold` comes from `SYNetInputFrame.buttons`
- `stick_range.x/y` come from `stick_x/stick_y`
- `button_tap` is computed from the previous published canonical buttons
- `button_release` is computed from the previous published canonical buttons
- `button_update` currently mirrors newly pressed buttons

`syNetInputPublishMainController()` mirrors player 0 into `gSYControllerMain` for systems that read the main controller global.

## Public API

`src/sys/netinput.h` exposes the current VS netplay input surface:

| Function | Role |
| -------- | ---- |
| `syNetInputReset()` | Reset slot sources and all input histories. |
| `syNetInputStartVSSession()` | Reset netinput state for a new VS match and default slots to local input. |
| `syNetInputGetTick()` | Return the current VS-local simulation tick used for input history. |
| `syNetInputSetTick()` | Set the VS-local tick; used by rollback/resimulation when rewinding to a prior tick frontier. |
| `syNetInputSetSlotSource()` | Select local, remote, predicted, or saved input for a player slot. |
| `syNetInputGetSlotSource()` | Inspect a player slot's current source. |
| `syNetInputSetRemoteInput()` | Stage a confirmed remote input sample for a tick. |
| `syNetInputSetSavedInput()` | Stage a saved/replay input sample for a tick. |
| `syNetInputGetHistoryFrame()` | Read the resolved input actually published for a player/tick. |
| `syNetInputGetPublishedFrame()` | Read the latest published input for a player. |
| `syNetInputGetHistoryChecksum()` | Produce a lightweight checksum over resolved input history for validation. |
| `syNetInputGetHistoryInputChecksum()` | Produce a source-independent checksum over published buttons/sticks for replay validation. |
| `syNetInputGetHistoryInputValueChecksumForPlayer()` | Source-independent checksum for one player across a contiguous tick span in `sSYNetInputHistory`. |
| `syNetInputGetHistoryInputValueChecksumWindow()` | Per-player checksums plus a folded combined checksum for a tick window. |
| `syNetInputSetRecordingEnabled()` | Enable or disable recording of resolved VS input frames. |
| `syNetInputGetRecordingEnabled()` | Inspect whether resolved VS input recording is enabled. |
| `syNetInputGetRecordedFrameCount()` | Read the number of VS ticks recorded since recording was enabled. |
| `syNetInputClearReplayFrames()` | Clear the full-match replay frame stream. |
| `syNetInputSetReplayFrame()` | Store one replay frame for a player and tick. |
| `syNetInputGetReplayFrame()` | Read one replay frame for a player and tick. |
| `syNetInputGetReplayInputChecksum()` | Produce a source-independent checksum over the full-match replay stream. |
| `syNetInputSetReplayMetadata()` | Stage metadata needed by future saved VS replay files. |
| `syNetInputGetReplayMetadata()` | Read staged VS replay metadata if available. |
| `syNetInputFuncRead()` | VS controller callback that resolves and publishes current-tick inputs. |

`src/sys/netreplay.h` exposes the debug runner surface:

| Function | Role |
| -------- | ---- |
| `syNetReplayInitDebugEnv()` | Read debug replay environment variables at port startup. |
| `syNetReplayStartVSSession()` | Configure record or playback state after a clean VS netinput session starts. |
| `syNetReplayUpdate()` | Write a record file at the frame limit or verify playback once enough frames have run. |
| `syNetReplayWriteDebugFile()` | Write explicit replay metadata and input frames to disk. |
| `syNetReplayLoadDebugFile()` | Load a debug replay file for playback. |

`src/sys/netpeer.h` exposes the debug UDP P2P surface:

| Function | Role |
| -------- | ---- |
| `syNetPeerInitDebugEnv()` | Read debug netplay environment variables and run optional match metadata bootstrap. |
| `syNetPeerStartVSSession()` | Open/reuse the UDP socket for VS, configure local/remote slot ownership, and enable the optional in-battle start barrier. |
| `syNetPeerCheckBattleExecutionReady()` | Return whether VS battle simulation/presentation may advance. Non-netplay and non-bootstrap sessions return true. |
| `syNetPeerCheckStartBarrierReleased()` | Return whether netinput may advance the VS-local tick. Non-netplay and non-bootstrap sessions return true. |
| `syNetPeerUpdateBattleGate()` | Receive control packets and drive `BATTLE_READY` / `BATTLE_START` while VS execution is held. |
| `syNetPeerUpdate()` | Receive packets, drive the start barrier, send local input frames, and log runtime stats. |
| `syNetPeerStopVSSession()` | Close the debug UDP socket and log the session summary. |

## VS Replay Direction

Saved VS replays are deterministic engine re-runs, not video captures. The current debug replay file includes:

- magic and version
- metadata size, frame size, frame count, player count, and input checksum
- VS scene/mode identifier
- initial battle settings and player slot setup
- stage, stocks/time/rules, item settings, characters, costumes, teams, handicaps, CPU levels, and match-start RNG seed
- per-player `SYNetInputFrame` stream
- source-independent input checksum for record/playback comparison

Playback resets the VS netinput session, loads metadata, stages full-match replay inputs, sets replayed slots to `nSYNetInputSourceSaved`, restores the match-start RNG seed, and runs the normal game engine.

The debug runner is controlled with environment variables:

```sh
cd build
SSB64_REPLAY_RECORD=/tmp/test.ssb64r SSB64_REPLAY_RECORD_FRAMES=1800 ./BattleShip
SSB64_REPLAY_PLAY=/tmp/test.ssb64r ./BattleShip
```

`SSB64_REPLAY_RECORD_FRAMES` is optional and defaults to 1800 frames. Record mode still uses the normal VS menus; playback mode loads the file and jumps directly into VS battle using the saved metadata.

## Debug P2P Netplay

`src/sys/netpeer.c` adds a UDP-only debug transport for manual P2P testing. It is not a final lobby or NAT traversal layer; it exists to prove that two local game instances can share match setup and exchange per-tick input frames.

Current debug environment variables:

- `SSB64_NETPLAY=1` enables the UDP P2P module.
- `SSB64_NETPLAY_LOCAL_PLAYER` selects the local slot index.
- `SSB64_NETPLAY_REMOTE_PLAYER` selects the remote slot index.
- `SSB64_NETPLAY_BIND` is the local IPv4 bind address in `host:port` form.
- `SSB64_NETPLAY_PEER` is the remote IPv4 address in `host:port` form.
- `SSB64_NETPLAY_DELAY` offsets sent input ticks by a fixed frame delay. It defaults to 2.
- `SSB64_NETPLAY_SESSION` optionally overrides the packet session id. It defaults to 1.
- `SSB64_NETPLAY_BOOTSTRAP=1` enables automatic VS match bootstrap.
- `SSB64_NETPLAY_HOST=1` marks the peer that creates and sends match metadata.
- `SSB64_NETPLAY_SEED` optionally overrides the bootstrap match RNG seed.
- `SSB64_NETPLAY_REMOTE_SLOTS` optional comma-separated **receiver** controller indices (e.g. `1` or `1,3`) whose **published vs remote-confirmed** histories participate in rollback mismatch scan. Defaults to `SSB64_NETPLAY_REMOTE_PLAYER` only.
- `SSB64_NETPLAY_EXTRA_LOCAL_PLAYER` optional second **sender** controller index on this machine; when set and valid, INPUT packets use **wire version 3** with a second bundled frame block for that slot (two humans on one PC talking to one peer). Leave unset for classic single-local packets (version 2).
- `SSB64_NETPLAY_PEER_SENDER_SLOTS` optional comma list of **sender** controller indices allowed from the peer’s packets (defaults to the peer’s primary slot = your `SSB64_NETPLAY_REMOTE_PLAYER`). Required when the peer uses dual-local senders so primary/secondary `player` bytes both validate.

Packet phases:

- `INPUT` packets carry recent local `SYNetInputFrame` samples, tagged with delayed VS-local ticks and staged into the remote confirmed input history.
- `MATCH_CONFIG` packets carry explicit `SYNetInputReplayMetadata` so both peers enter the same VS battle with the same stage, rules, players, characters, and RNG seed.
- Bootstrap `READY` / `START` packets complete the pre-VS metadata handshake.
- In-battle `BATTLE_READY` / `BATTLE_START` packets align the VS-local netinput tick before runtime input packets begin.

Match metadata sync, input tick start sync, and VS execution sync are separate layers:

- Metadata bootstrap makes both peers enter the same battle state.
- The input tick barrier keeps `syNetInputGetTick()` at 0 until both peers have reached VS and exchanged readiness.
- The VS execution gate keeps `scVSBattleFuncUpdate()` from advancing battle/interface presentation while the bootstrap barrier is still waiting.

The execution gate is intentionally shaped as a reusable readiness query. Future runtime pacing, peer advertised ticks, and rollback readiness checks should build on this boundary instead of adding more one-off checks to the VS scene.

Bootstrap P2P input packets (`INPUT`, wire version `SYNETPEER_VERSION` 2):

- Carry a strictly increasing UDP send `packet_seq`, included in the packet checksum and surfaced as cumulative `gap` / `dup` / `ooo` counters when sequence jumps repeat, advance with holes, or arrive behind the observed high watermark.
- Bundle the last 16 simulated local frames (`SYNETPEER_MAX_PACKET_FRAMES`), each with delayed tick + buttons/stick, serialized explicitly (not raw struct casts).
- Still embed `ack_tick`: the sender’s tracked `sSYNetPeerHighestRemoteTick` advertised to the peer (see `peer_ack=` / `puck=` lines in logs).

Operational desync instrumentation (logged only when UDP netplay is active and bootstrap execution is released):

| Log prefix | Approx. cadence | Use |
| ---------- | ----------------- | --- |
| `SSB64 NetPeer:` | Every 120 sim ticks (`SYNETPEER_LOG_INTERVAL`) | Transport counters, sequence diagnostics, staged frames, cumulative remote-input fingerprint (`inpchk`). |
| `SSB64 NetSync:` | Same ticks as NetPeer summaries | **`hist_win=[begin,end)`** — half-open `[begin,end)` VS tick range hashed from **resolved published history only** (`sSYNetInputHistory`) using the same logical fields as replay validation (player id + tick + buttons + sticks). **`all`** / **`p0..p3`** show combined and per-slot checksums. **`figh`** is `syNetSyncHashBattleFighters()` over active fighter `FTStruct` scalars plus selected velocities, `coll_data.pos_prev`, TopN root translate, and `status_total_tics` using IEEE754 bit reinterpretation — order-independent across controller ports. **`mph`** (when present) is `syNetSyncHashMapCollisionKinematics()` over `gMPCollisionUpdateTic` and capped yakumono mover state for rollback bisect. **`pko`/`pkn`** are oldest/newest frame ticks bundled in the most recent validated remote `INPUT` packet (or sentinel `4294967295` when **`pkt_valid=0`**). **`gap`/`dup`/`ooo`** track inferred sequence anomalies. Trailing **`delay`/`ring`/`rscan`** echo `SSB64_NETPLAY_DELAY` (possibly adaptive), `SYNETINPUT_HISTORY_LENGTH` (rollback ring depth), and `SYNETROLLBACK_SCAN_WINDOW`. |

Debug workflow:

1. After metadata bootstrap and execution gate, confirm **`SSB64 NetSync`** lines appear on matching ticks across host/client logs.
2. If **`hist_win`** checksum columns diverge first, prioritize packet redundancy, deserialization, staging order, tick assignment, or prediction quirks before rewriting rollback.
3. If input windows match while **`figh`** diverges, widen or narrow deterministic gameplay hashes before blaming UDP pacing.
4. If both stay aligned but observers still perceive drift, escalate to pacing / telemetry using the same instrumentation hooks.

`cmake` discovers `src/sys/*.c` through `file(GLOB_RECURSE …)`; adding a netplay sys source requires re-running CMake configuration (for example `cmake -B build`) so the glob refreshes before the next build.

## Validation Path

Before adding sockets or rollback state restoration, use the saved-input path to validate deterministic VS input replay:

1. Run a local battle with all slots set to local input.
2. Capture `sSYNetInputHistory` through `syNetInputGetHistoryFrame()`.
3. Re-stage those samples with `syNetInputSetSavedInput()`.
4. Set the relevant slots to `nSYNetInputSourceSaved`.
5. Compare `syNetInputGetHistoryInputChecksum()` against the replay file checksum.

This verifies the input layer can reproduce the same per-tick controller stream before introducing rollback state rewind.

## Rollback netcode (in implementation)

Rollback builds on the existing input boundary: peers agree on **simulation tick indices** and **inputs per tick**; when confirmed remote input disagrees with what was **published** for that tick, the session **restores** saved gameplay state and **resimulates** forward with corrected inputs.

**Implementation (PORT, first landing):** [`src/sys/netrollback.c`](src/sys/netrollback.c) / [`src/sys/netrollback.h`](src/sys/netrollback.h). VS saves **post-tick** fighter scalars (aligned with `syNetSyncHashBattleFighters()` fields plus velocities, `pos_prev`, TopN root translate, `status_total_tics`, **aerial knockback velocity** `vel_damage_air`, and **`hitlag_tics`**) plus a minimal **map / yakumono** slice (`gMPCollisionUpdateTic`, per-mover translate, `gMPCollisionSpeeds[]`, and yakumono `user_data.s`) into a ring sized like `SYNETINPUT_HISTORY_LENGTH`. Map state is **applied before fighters** on rollback load. `syNetRollbackAfterBattleUpdate()` runs at the end of [`scVSBattleFuncUpdate`](src/sc/sccommon/scvsbattle.c). `syNetRollbackUpdate()` runs from [`syNetPeerUpdate()`](src/sys/netpeer.c): scans recent ticks for mismatch between published history and remote confirmed history on **every** controller index listed in `SSB64_NETPLAY_REMOTE_SLOTS` (default: `SSB64_NETPLAY_REMOTE_PLAYER`), loads snapshot `mismatch_tick - 1`, then resimulates using `syNetInputFuncRead()` + `scVSBattleFuncUpdate()` until the current frontier. While resimulating, [`syNetPeerUpdateBattleGate()`](src/sys/netpeer.c) and [`syNetPeerUpdate()`](src/sys/netpeer.c) return early so recv/send do not recurse.

| Env var | Effect |
| ------- | ------ |
| `SSB64_NETPLAY_ROLLBACK=0` | Disable rollback snapshots + mismatch resim. Fixed sim pacing via taskman intervals still applies whenever a VS UDP session is active (`syNetPeerIsVSSessionActive()`). Default: rollback **on** when unset. |
| `SSB64_NETPLAY_SIM_HZ` | Target simulation Hz for pacing (default **60**). Used by `syNetRollbackApplyPortSimPacing()` from [`port/gameloop.cpp`](port/gameloop.cpp) with the window refresh rate. |
| `SSB64_NETPLAY_ROLLBACK_INJECT_TICK=N` | **Debug:** Select wire tick `N` (packet frame tick = sender history tick + delay) for a one-shot harness. Without `FORCE_MISMATCH`, XORs incoming remote **payload** buttons once before staging (may not create `history≠remote` on loopback if staging runs before the first `FuncRead` that consumes tick `N`). |
| `SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH=1` | **Debug:** With `INJECT_TICK`, do **not** tamper the wire. After remote is staged and once `syNetInputGetTick() > N`, XOR **`0x1000` into published history only** if history and remote still agree — guarantees `history≠remote` for mismatch detection on fast LAN. Logs `FORCE_MISMATCH armed` / `detected published history == remote` / `gave up` as appropriate. |
| `SSB64_NETPLAY_ROLLBACK_MISMATCH_DEBUG=1` | **Debug:** While scanning for mismatches, log up to **16** per-VS-session cases where exactly one of `{published history, remote history}` exists for the remote slot at a tick inside the scan window (explains silent skips when NetSync diverges but rollback never fires). |
| `SSB64_NETPLAY_ROLLBACK_VERIFY_STRICT=1` | **Debug:** After a successful resim, if `syNetSyncHashBattleFighters()` is unchanged vs the pre-resim hash, log a **VERIFY_STRICT** warning (possible no-op snapshot or single-tick invisible delta). |
| `SSB64_NETPLAY_ROLLBACK_FORCE_MISMATCH_PLAYER=N` | **Debug:** With `FORCE_MISMATCH`, XOR published history for controller port **`N`** instead of the first `SSB64_NETPLAY_REMOTE_SLOTS` entry. **`N`** must appear in that receive list. |
| `SSB64_NETPLAY_ADAPTIVE_DELAY=1` | When set, raises `SSB64_NETPLAY_DELAY` by 1 toward `SSB64_NETPLAY_DELAY_MAX` when rollback load-fails (`lf` delta) or late remote frames spike (`late` delta), and lowers it after stable intervals; floor is the delay parsed at startup from `SSB64_NETPLAY_DELAY`. |
| `SSB64_NETPLAY_DELAY_MAX` | Ceiling for adaptive delay (default **12**). Ignored when adaptive delay is off. |
| `SSB64_NETPLAY_PREDICT_NEUTRAL=1` | Use neutral sticks/buttons for **predicted** remote frames instead of repeating the last confirmed sample (more rollbacks, useful for stress testing). |

### Rollback load-fail triage (`lf` counter)

`lf` in **`SSB64 NetSync`** lines is [`syNetRollbackGetLoadFailCount()`](src/sys/netrollback.c): each increment means resim could not load a post-tick snapshot for **`mismatch_tick - 1`** (ring slot missing or overwritten). Typical causes:

1. **Misprediction older than the ring** — effective rollback depth exceeds `SYNETINPUT_HISTORY_LENGTH` (720) or the mismatch scan window (`SYNETROLLBACK_SCAN_WINDOW` = 256). Increase `SSB64_NETPLAY_DELAY`, reduce packet loss, or shrink how far behind confirmed inputs can arrive.
2. **Transport burst** — correlate with **`late`**, **`gap`**, and **`dup`/`ooo`** on the same log line; adaptive delay (above) is intended to react to `lf`/`late` spikes.
3. **First frames** — rare edge at match start; if persistent, verify snapshots are saved (`rb` rollback count and NetPeer ordering).

Coordinator log on failure: `load post tick … failed (need earlier snapshots; check delay/loss vs ring=720 scan=256)`.


### Rollback harness (loopback vs production)

- **Production-like prediction error:** late UDP causes `FuncRead` to publish **prediction** for tick `t`, then a later packet fills **`remoteHistory[t]`** with truth; mismatch scan sees **`history[t]≠remote[t]`** and rollback runs.
- **Loopback / low latency:** remote is often staged **before** the first publish for tick `t`, so **`history[t]`** and **`remote[t]`** already match — wire XOR does not create a mismatch. Use **`FORCE_MISMATCH`** to validate snapshot load + resim anyway.

### Determinism bisect (when `figh` diverges but inputs match)

1. Fix **`SSB64_NETPLAY_SEED`**, delay, characters, and stage; capture host + client logs with **`SSB64 NetSync`** at the same nominal tick cadence.
2. Note the **earliest** `hist_win` where **`figh`** (or `p*`) first disagrees across peers.
3. If **`all` / `p*`** already disagree, fix input staging, tick mapping, or packet redundancy before widening snapshots.
4. If **inputs agree** but **`figh`** disagrees, treat as **simulation divergence**: extend [`syNetSyncHashBattleFighters()`](src/sys/netsync.c) and rollback snapshots together until the first divergent subsystem is identified (RNG, items, articles, collision scratch, etc.).
5. Use **`SSB64_NETPLAY_ROLLBACK_MISMATCH_DEBUG`** when rollback stays silent despite suspected input issues — look for **history-only** or **remote-only** slots inside the scan window.

**Design reference (Cursor plan `netplay_rollback_plan_cce27cd2.plan.md`):**

| Layer | Responsibility |
| ----- | ---------------- |
| **Fixed SIM_HZ** | Negotiated logical rate (default **60 Hz**); port calls `syTaskmanSetIntervals(K, 1)` so peers stay tick-aligned across monitor refresh differences. |
| **Snapshots** | Expand fighter/world capture until full match resim is stable; v1 is intentionally narrow. |
| **Rollback coordinator** | `syNetRollbackUpdate()` + `syNetInputRollbackPrepareForResim()` in [`netinput.c`](src/sys/netinput.c). |
| **Netpeer** | `SSB64_NETPLAY_DELAY`, redundant frames, `syNetPeerGet*` accessors for frontier/slot. |
| **Verification** | NetSync `figh` + per-interval `rb`/`lf` rollback counters; post-resim logs in `syNetRollbackUpdate`; `SSB64_NETPLAY_ROLLBACK_*` harness env (see env table above). |

**Explicitly out of snapshot v1:** framebuffer contents, full audio buffer rewind, raw heap dumps — same spirit as existing non-goals.

## Future work: simulation cadence vs monitor refresh (pipeline revisit)

Rollback and lockstep netplay require peers to agree on **simulation tick indices** and **inputs per tick**. That is **not** the same as monitor refresh rate or render FPS: tying `task_update` / netinput advancement 1:1 to uncapped VI/`PortPushFrame` lets two machines drift by tick rate (e.g. 120 Hz vs 60 Hz clients).

**Direction:** Fix **session simulation Hz** (typically 60 to match original tempo)—including bundling multiple VI messages per logical update where needed (see `syTaskmanSetIntervals()` in `src/sys/taskman.c`)—while letting users keep **custom resolution and display settings** for presentation. Resolution stays in render/game-unit space; framebuffer rollback remains out of scope.

**Competitive display strategy (baseline):** Prefer **duplicate-frame presentation** at high refresh (same sim output shown twice) or **VRR** pacing over mandatory **visual interpolation**. Interpolation can remain an optional graphics toggle later; it affects perceived motion and visual latency relative to sim, not necessarily gameplay sampling, but many competitors prefer truthfulness over blended poses.

**Long-term:** Revisit the **core game loop / taskman / port** boundary with maintainers to make **sim stepped at SIM_HZ**, **render reading last committed state**, and **network/rollback** keyed only to sim ticks—so netplay (including future 4P/customs) stays one logical timeline while presentation varies per machine. Track this when proposing wider patches to `taskman`, scheduler, or `port/gameloop.cpp`.

## Non-Goals

Still **not** in scope (or not implied by rollback v1):

- matchmaking, lobby, ELO, region, or ping selection
- STUN/TURN, NAT traversal, or relay fallback
- framebuffer rollback (render output is not simulation state)
- exhaustive determinism hashing of fighter/world state (only narrow diagnostic hashes ship today via `netsync.c`; snapshots must still capture full gameplay determinism — wider **hashing** for debug remains optional)
- netplay support for 1P Game, Training Mode, Bonus 1 Practice, or Bonus 2 Practice

**Rollback-related:** PORT VS UDP builds ship an initial **snapshot / mismatch / resim** stack (`src/sys/netrollback.c`). Fighter capture is intentionally narrow for v1 — expand as full-match determinism testing exposes gaps; details in [Rollback netcode (in implementation)](#rollback-netcode-in-implementation).

Those systems should build on top of this input boundary rather than bypassing it.

## Rollback validation backlog (circle back)

**Strategy:** Do not block the next rollback milestones on exhaustive proof that **mid-air** and **mid-air hit** snapshot fields (`vel_damage_air`, `hitlag_tics`, etc.) match NetSync under every combat state. Keep **Fox up+B** and **moving-platform** edge cases as informal gameplay stress while extending snapshots and transport.

**Roadmap (implementation order):** (1) **Multi-remote** — rollback scans every VS remote human slot; netpeer may bundle a second local sender in **INPUT v3** when configured (see env table additions in debug P2P). (2) **Stage / platform kinematics** — rollback ring captures a minimal **map collision** slice (`gMPCollisionUpdateTic` + per-yakumono pose/speed/status) before fighters; NetSync logs optional **`mph=`** hash for bisect. Revisit the checklist below after those land — much of it should get incidental coverage from 3P/4P-style remote lists and platform-heavy stages.

**Circle-back checklist (narrow hash / harness gaps):**

| Item | What to confirm later |
|------|------------------------|
| Load-fail **`lf`** | [`syNetRollbackLoadPostTick`](src/sys/netrollback.c) / `load post tick … failed` vs ring wrap if scan window or ring length change. |
| Fighter blob under combat | [`syNetRollbackCaptureFighters`](src/sys/netrollback.c) / [`syNetRollbackApplyFighters`](src/sys/netrollback.c) for aerial KB + hitlag during real forced resim (not only early-tick harness). |
| Two-peer **`figh`** | [`syNetSyncHashBattleFighters`](src/sys/netsync.c) vs peer logs at the same tick cadence; bisect per [Determinism bisect](#determinism-bisect-when-figh-diverges-but-inputs-match). |
| **`mph`** (map hash) | Optional NetSync `mph=` — extend alongside snapshot fields when movers still diverge. |
| **`VERIFY_STRICT`** | Interpreting “`figh` unchanged post-resim” vs invisible deltas / narrow hash; adjust or widen hash if noisy. |

See [Debug P2P Netplay](#debug-p2p-netplay) for new multi-remote env vars when operating dual-local senders or non-default remote receive slot lists.
