# Matchmaking server debug harness (BattleShip-P2P)

This describes how to drive the **BattleShip-Server** automatch HTTP API while keeping the game on the existing **`SSB64_NETPLAY_*` environment contract** (see [`netplay_architecture.md`](netplay_architecture.md) and [`netcode_agent_rules.md`](netcode_agent_rules.md)).

The sibling server repo documents the HTTP API in its own tree: `BattleShip-Server/docs/MATCHMAKING.md` (path relative to your checkout).

## Phase A — Shell only (no libcurl in the game)

Script: [`scripts/mm_smoke_two_clients.sh`](../scripts/mm_smoke_two_clients.sh)

1. Start **BattleShip-Server** (default `http://127.0.0.1:8080`).
2. Run the script; it creates two players, queues both, polls until matched, and prints **two blocks of `export` statements**.
3. In **terminal A**, paste the Client A block; in **terminal B**, paste Client B; then run `./BattleShip` from `build/` in each (working directory must see `BattleShip.o2r` etc. per your usual workflow).

Environment overrides for the script:

| Variable | Default | Meaning |
|----------|---------|---------|
| `MM_BASE_URL` | `http://127.0.0.1:8080` | Server base URL (or pass as first script arg). |
| `MM_BIND_A` / `MM_BIND_B` | `127.0.0.1:7001` / `7002` | Must match `udp_endpoint` sent in `POST /v1/queue` and the `SSB64_NETPLAY_BIND` the script prints. |
| `MM_REGION` | `na-east` | Queue bucket. |
| `MM_GAME_VERSION` | `dev` | Queue bucket. |
| `MM_POLL_SLEEP_SEC` | `0.25` | Poll interval between `GET /v1/match/...`. |
| `MM_HEARTBEAT_EVERY` | `10` | Heartbeat every N poll iterations per client. |

**Loopback / LAN note:** For same-machine smoke tests, `udp_endpoint` is the reflexive address you advertise to the other peer. On `127.0.0.1` with two processes, use two fixed ports and bind each process to its own port—**STUN is not required**. Real NAT tests later must use STUN on the **same UDP socket** netpeer binds; that is not implemented in this harness yet.

## Phase B — In-process bootstrap (`SSB64_MM_AUTO`)

If CMake finds **libcurl**, the game defines `SSB64_HAVE_LIBCURL` and links `CURL::libcurl`. Then:

| Variable | Required | Meaning |
|----------|----------|---------|
| `SSB64_MM_AUTO` | Yes (`1`) | Run HTTP bootstrap before `syNetPeerInitDebugEnv()`. |
| `SSB64_MM_BASE_URL` | No | Default `http://127.0.0.1:8080`. |
| `SSB64_MM_BIND` | Yes | `host:port` for **both** queue JSON and `SSB64_NETPLAY_BIND`. |
| `SSB64_MM_PLAYER_ID` / `SSB64_MM_API_TOKEN` | No | If unset, `POST /v1/players` is used once and tokens are logged (dev only). |
| `SSB64_MM_REGION` / `SSB64_MM_GAME_VERSION` | No | Defaults `na-east` / `dev`. |
| `SSB64_MM_LOCAL_PLAYER` / `SSB64_MM_REMOTE_PLAYER` | No | Default `0` / `1`. |
| `SSB64_MM_NETPLAY_DELAY` | No | Default `2`. |
| `SSB64_MM_POLL_TIMEOUT_SECS` | No | Default `300` seconds waiting for a match. |

Without libcurl, `SSB64_MM_AUTO=1` logs a one-line hint and does nothing—use the shell script.

Bootstrap runs **synchronously** during startup (brief UI stall until matched or timeout).

## Phase C — Heartbeat from the game loop (optional long-queue test)

If you need `POST /v1/heartbeat` while the game is already running (e.g. you exported queue state yourself), set:

- `SSB64_MM_HEARTBEAT_TICKET` — ticket UUID from `POST /v1/queue`
- `SSB64_MM_PLAYER_ID` / `SSB64_MM_API_TOKEN` / `SSB64_MM_BASE_URL` — same as API auth

Each frame path calls `port_mm_game_heartbeat_tick()` (from `port/gameloop.cpp`); it POSTs at most once every **2 seconds** when the env vars are present. Clear `SSB64_MM_HEARTBEAT_TICKET` when no longer queued.

## Verification checklist

- Server log line `matched` and a row in SQLite `matches`.
- Both clients: `SSB64 NetPeer` / `SSB64 NetSync` logs align as in existing netplay debug docs.
