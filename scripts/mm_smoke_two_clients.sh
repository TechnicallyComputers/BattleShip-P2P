#!/usr/bin/env bash
# Smoke-test BattleShip-Server automatch from two local BattleShip clients.
# Requires: curl, jq, BattleShip-Server running (default http://127.0.0.1:8080).
#
# Usage:
#   ./scripts/mm_smoke_two_clients.sh [BASE_URL]
#
# Prints two shell blocks: eval the "Client A" block in terminal A, then
# "Client B" in terminal B, then start BattleShip from build/ in each.
#
# For same-host UDP, use distinct bind ports; udp_endpoint in the queue body
# must match what you pass as SSB64_NETPLAY_BIND (STUN not required on loopback).

set -euo pipefail

BASE_URL="${1:-${MM_BASE_URL:-http://127.0.0.1:8080}}"
BASE_URL="${BASE_URL%/}"

BIND_A="${MM_BIND_A:-127.0.0.1:7001}"
BIND_B="${MM_BIND_B:-127.0.0.1:7002}"
REGION="${MM_REGION:-na-east}"
GAME_VERSION="${MM_GAME_VERSION:-dev}"
POLL_SLEEP_SEC="${MM_POLL_SLEEP_SEC:-0.25}"
HEARTBEAT_EVERY="${MM_HEARTBEAT_EVERY:-10}"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing command: $1" >&2
    exit 1
  }
}

need_cmd curl
need_cmd jq

json_post() {
  local url="$1"
  shift
  curl -sS -f -H "Content-Type: application/json" -d "$1" "$url"
}

json_get() {
  curl -sS -f "$1"
}

json_post_auth() {
  local url="$1"
  local pid="$2"
  local tok="$3"
  local body="$4"
  curl -sS -f \
    -H "Content-Type: application/json" \
    -H "X-Player-Id: ${pid}" \
    -H "Authorization: Bearer ${tok}" \
    -d "${body}" \
    "${url}"
}

create_player() {
  json_post "${BASE_URL}/v1/players" '{}'
}

queue_player() {
  local pid="$1" tok="$2" endpoint="$3"
  local body
  body="$(jq -nc \
    --arg ep "$endpoint" \
    --arg rg "$REGION" \
    --arg gv "$GAME_VERSION" \
    '{udp_endpoint:$ep,region:$rg,game_version:$gv}')"
  json_post_auth "${BASE_URL}/v1/queue" "$pid" "$tok" "$body"
}

heartbeat() {
  local pid="$1" tok="$2" ticket="$3"
  local body
  body="$(jq -nc --arg t "$ticket" '{ticket_id:$t}')"
  json_post_auth "${BASE_URL}/v1/heartbeat" "$pid" "$tok" "$body" >/dev/null
}

poll_until_matched() {
  local ticket="$1" pid="$2" tok="$3"
  local n=0
  while true; do
    local resp
    resp="$(json_get "${BASE_URL}/v1/match/${ticket}")"
    local st
    st="$(echo "$resp" | jq -r .status)"
    if [[ "$st" == "matched" ]]; then
      echo "$resp"
      return 0
    fi
    if (( n % HEARTBEAT_EVERY == 0 )); then
      heartbeat "$pid" "$tok" "$ticket" || true
    fi
    n=$((n + 1))
    sleep "${POLL_SLEEP_SEC}"
  done
}

echo "==> MM smoke: ${BASE_URL}" >&2
echo "==> Queue endpoints: A=${BIND_A} B=${BIND_B}" >&2

PA="$(create_player)"
PB="$(create_player)"

PID_A="$(echo "$PA" | jq -r .player_id)"
TOK_A="$(echo "$PA" | jq -r .api_token)"
PID_B="$(echo "$PB" | jq -r .player_id)"
TOK_B="$(echo "$PB" | jq -r .api_token)"

QA="$(queue_player "$PID_A" "$TOK_A" "$BIND_A")"
QB="$(queue_player "$PID_B" "$TOK_B" "$BIND_B")"

TICK_A="$(echo "$QA" | jq -r .ticket_id)"
TICK_B="$(echo "$QB" | jq -r .ticket_id)"

echo "==> Queued tickets: A=${TICK_A} B=${TICK_B}" >&2

# Poll both in background so matcher can pair; print when both matched.
R_A="$(mktemp)"
R_B="$(mktemp)"
poll_until_matched "$TICK_A" "$PID_A" "$TOK_A" >"$R_A" &
PID_JOB_A=$!
poll_until_matched "$TICK_B" "$PID_B" "$TOK_B" >"$R_B" &
PID_JOB_B=$!
wait "$PID_JOB_A"
wait "$PID_JOB_B"

MATCH_A="$(cat "$R_A")"
MATCH_B="$(cat "$R_B")"
rm -f "$R_A" "$R_B"

peer_a="$(echo "$MATCH_A" | jq -r .match.peer)"
sess_a="$(echo "$MATCH_A" | jq -r .match.session_id)"
host_a="$(echo "$MATCH_A" | jq -r .match.you_are_host)"

peer_b="$(echo "$MATCH_B" | jq -r .match.peer)"
sess_b="$(echo "$MATCH_B" | jq -r .match.session_id)"
host_b="$(echo "$MATCH_B" | jq -r .match.you_are_host)"

echo ""
echo "########## Client A — copy/paste into shell, then run BattleShip ##########"
cat <<EOF
export SSB64_NETPLAY=1
export SSB64_NETPLAY_BOOTSTRAP=1
export SSB64_NETPLAY_LOCAL_PLAYER=0
export SSB64_NETPLAY_REMOTE_PLAYER=1
export SSB64_NETPLAY_BIND='${BIND_A}'
export SSB64_NETPLAY_PEER='${peer_a}'
export SSB64_NETPLAY_SESSION='${sess_a}'
export SSB64_NETPLAY_HOST='${host_a}'
export SSB64_NETPLAY_DELAY='${SSB64_NETPLAY_DELAY:-2}'
EOF

echo ""
echo "########## Client B — copy/paste into shell, then run BattleShip ##########"
cat <<EOF
export SSB64_NETPLAY=1
export SSB64_NETPLAY_BOOTSTRAP=1
export SSB64_NETPLAY_LOCAL_PLAYER=0
export SSB64_NETPLAY_REMOTE_PLAYER=1
export SSB64_NETPLAY_BIND='${BIND_B}'
export SSB64_NETPLAY_PEER='${peer_b}'
export SSB64_NETPLAY_SESSION='${sess_b}'
export SSB64_NETPLAY_HOST='${host_b}'
export SSB64_NETPLAY_DELAY='${SSB64_NETPLAY_DELAY:-2}'
EOF

echo ""
echo "==> Done. Player IDs (for /result): A=${PID_A} B=${PID_B}" >&2
