# Netplay VS menus (`port/net/`, `SSB64_NETMENU`)

This document describes **how BattleShip forks decomp translation units into `port/net/`** for the VS **netmenu** profile, how those forks are wired in CMake, and what each migrated surface is supposed to do. Net input, replay, and peer code under `port/net/sys/` is covered at a higher level in [netplay_architecture.md](./netplay_architecture.md); this file focuses on **menu UX and scene/menu forks**.

Enable the profile when configuring CMake:

```sh
cmake -B build -DSSB64_NETMENU=ON
```

That defines `SSB64_NETMENU=1` for the game object library.

---

## Why fork instead of patching `decomp/`?

The `decomp` tree tracks ROM-faithful game code. BattleShip-exclusive flows (online hub rows, PNG labels, ban masks, SC kinds) belong in **`port/net/`** so the submodule stays a clean reference. **Do not** add netmenu-only behavior by editing files under `decomp/src/` or `decomp/include/` when a port fork is sufficient.

---

## Protocol: adding or refreshing a fork

1. **Pick the authoritative decomp TU**  
   Example: VS Mode hub is `decomp/src/mn/mnvsmode/mnvsmode.c`.

2. **Copy into `port/net/` with a predictable layout**  
   - Prefer subdirectories that mirror subsystem: `port/net/menus/` for MN, `port/net/sc/` for SC, `port/net/lb/` for LB, etc.  
   - Use a filename that avoids symbol collisions **or** deliberately replaces the TU (see CMake below). Typical patterns:
     - **Replace-symbol TU**: fork keeps the same *logical role* but may use a distinct filename (e.g. `mnvsmodenet.c` replacing the compiled role of `mnvsmode.c`).
     - **New scene**: fork of something like `mnplayers1pgame.c` renamed and prefixed (`scautomatch.c` with `mnVSNet*` symbols).

3. **Document the pedigree at top of file**  
   Header comment should state:
   - Original decomp path(s).
   - That the file compiles **only with** `-DSSB64_NETMENU=ON` (if true).
   - Entry symbol / scene kind if non-obvious.

4. **Gate port-only divergence**  
   Use `#ifdef SSB64_NETMENU` (and existing `PORT` patterns) inside the fork so stock builds (`SSB64_NETMENU` OFF) never see unused symbols breaking linkage. CMake already adds `SSB64_NETMENU` to compile definitions only when the option is ON.

5. **Wire CMake (mandatory)**  
   In root `CMakeLists.txt`, inside `if(SSB64_NETMENU)`:
   - **`list(FILTER … EXCLUDE)`** removes the matching **decomp** `.c` from `SSB64_DECOMP_SOURCES` so you do not compile two definitions of the same symbols.
   - **`target_sources(ssb64_game PRIVATE …)`** adds each **port** `.c` explicitly.  
   **`port/net/**/*.c` is excluded from the generic `port/*.c` glob** — net sources are never picked up accidentally.

6. **Header shadows (when needed)**  
   Some menus need a **local** `scene.h` / `lbcommon.h` that includes port-only declarations or different include order. CMake adds `BEFORE` include directories:

   - `port/net/lb`
   - `port/net`

   so `port/net/sc/scene.h` can pull `sctypes.h` from the same tree and so `lbcommon.h` can resolve to `port/net/lb/lbcommon.h`. If a menu must force the shadow after decomp already included a guarded header, the fork may use the same **undef guard** pattern as in `port/net/menus/mnvsonline_maps.c` before `#include <lbcommon.h>`.

7. **Verify**  
   Configure with `SSB64_NETMENU=ON` and build `ssb64`. Also confirm `SSB64_NETMENU=OFF` still builds if your change could affect shared headers.

8. **Update this document**  
   Add a row to [Migrated files and behavior](#migrated-files-and-behavior) when you add or substantially change a fork.

---

## PNG assets

Runtime loads menu art from the **executable directory** under `port/net/assets/` (CMake copies listed PNGs next to the binary). Helpers live in `port/net/mn_vs_submenu_png.c` / `.h` (`mnPortTryApplyVsSubmenuLabelPng`, etc.). New assets need both **source files under** `port/net/assets/` and **CMake `add_custom_command` copy rules** alongside the existing PNG entries.

---

## Migrated files and behavior

| Port file | Decomp / origin (approx.) | Role / notes |
| --- | --- | --- |
| `port/net/menus/mnvsmodenet.c` | `decomp/src/mn/mnvsmode/mnvsmode.c` | VS Mode **hub**: replaces stock `mnvsmode.c` when built with netmenu. Cascading tab layout, routes to offline/online/automatch/level-prefs/customs-style rows, PNG label integration via `mn_vs_submenu_png.h`. |
| `port/net/menus/mnvsoffline.c` | VS rules flow (decomp-derived) | **Offline** tier: ROM-faithful VS rules UI; Back returns to net hub (`nSCKindVSMode`). |
| `port/net/menus/mnvsonline.c` | Pattern from hub / new rows | **Online** tier: netplay feature submenu (stub rows acceptable); B back to hub. |
| `port/net/menus/mnvsonline_maps.c` | `decomp/src/mn/mnmaps/mnmaps.c` (fork) | **Level prefs** scene (`nSCKindVSNetLevelPrefs`): stage grid, wallpaper preview, training vs versus path, **stage ban** mask, grey/dim icon + preview when banned; relies on `lbCommonDrawSObjChainDecalAsPrimMultiply` and **non–FASTCOPY** grid icons for tinting. |
| `port/net/menus/mnvsresults.c` | `decomp/src/mn/mnvsmode/mnvsresults.c` (fork) | **VS results** only when netmenu: after a match launched from **HTTPS automatch**, exiting results returns to `nSCKindVSNetAutomatch` using `gSCManagerSceneData.is_vs_automatch_battle` + `vs_net_automatch_post_battle_scene` (see `sctypes.h`). Stock netmenu VS still returns to `nSCKindPlayersVS`. |
| `port/net/mn_vs_submenu_png.c` | N/A (port-only) | Loads RGBA PNGs for VS submenu labels / decorations. |
| `port/net/sc/scmanager.c` | `decomp/src/sc/scmanager.c` | Scene manager fork: branches to netmenu start functions (`mnVSModeOnlineStartScene`, `mnVSNetAutomatchStartScene`, `mnVSNetLevelPrefsStartScene`, …) when `SSB64_NETMENU`. |
| `port/net/sc/sccommon/scautomatch.c` | `decomp/src/mn/mnplayers/mnplayers1pgame.c` (pattern fork) | **Automatch** scene (`nSCKindVSNetAutomatch`); Back to online. On Linux + netmenu, START runs **HTTP(S) matchmaking API + STUN reflexive endpoint + `syNetPeer` P2P** (not `nSCKind1PGame`). |
| `port/net/lb/lbcommon.c` | `decomp/src/lb/lbcommon.c` | LB draw fork: `SSB64_NETMENU` helpers such as **`lbCommonDrawSObjChainDecalAsPrimMultiply`** for prim-modulated CI sprites (ban tints, menu labels). |
| `port/net/lb/lbcommon.h` | Shadow of decomp `lbcommon.h` | Declarations for netmenu-only LB APIs. |
| `port/net/sc/scene.h` | Mirror of decomp `scene.h` | Includes local `sctypes.h` so new SC kinds / tables can be maintained next to port SC code. |
| `port/net/sc/sctypes.h` | Extended from decomp | Netmenu **scene kinds** and related types (keep in sync with SC setup tables in decomp overlay where required). |

Other files under `port/net/` (for example `port/net/sc/sccommon/scvsbattle.c`) may exist for experiments; **only TUs listed in `CMakeLists.txt` under `SSB64_NETMENU` are part of the supported netmenu build**. If you wire a new TU, add it there and to the table above.

### Automatch HTTP API (Linux netmenu)

- **Sources:** `port/net/matchmaking/mm_matchmaking.c` (libcurl + pthread), `mm_stun.c` (RFC 5389 on the UDP fd netpeer will use).
- **Default API base:** `http://127.0.0.1:8899` (compile-time `MM_DEFAULT_BASE_URL` in `mm_matchmaking.c`). Override with **`SSB64_MATCHMAKING_BASE_URL`** for production or other hosts.
- **Credentials file:** `$XDG_CONFIG_HOME/ssb64/matchmaking.cred` or `~/.config/ssb64/matchmaking.cred` (`PLAYER_ID=…`, `API_TOKEN=…`).
- **Useful env:** `SSB64_MATCHMAKING_PUBLIC_ENDPOINT=host:port` (skip STUN), `SSB64_MATCHMAKING_BIND=host:port` (override local bind passed to netpeer).
- **Local server:** Run `BattleShip-Server` with **`BIND_ADDR=127.0.0.1:8899`** (or `0.0.0.0:8899`). Use **`--debug`** for request tracing and structured matchmaking logs (`RUST_LOG` still overrides filter directives).
- **CMake:** On non-Windows, netmenu enables `find_package(CURL)` / Threads, links **`CURL::libcurl`** into the game binary, adds **`port/net/matchmaking`** to include path, excludes **`decomp/.../mnvsresults.c`**, and compiles the fork + matchmaking `.c` files.

---

## Related documentation

- [netplay_architecture.md](./netplay_architecture.md) — `port/net/` policy summary, input/replay/peer scope.
- Root [CLAUDE.md](../CLAUDE.md) — worktree workflow and project conventions.

When a netmenu change fixes a user-visible bug worth remembering, add a short entry under `docs/bugs/` and link it from `docs/bugs/README.md` per project convention.
