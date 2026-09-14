#!/bin/sh
# Prove key broadcast: run one ROM in four tiles, have the strip publish a fixed key
# pattern (PJ64_GRID_SELFTEST), and check that every tile's input plugin read it.
# Design: Docs/superpowers/specs/2026-09-14-multi-rom-grid-design.md
set -eu

ROM="${1:?usage: grid_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
LOG="$(mktemp)"
TILES=4
TIMEOUT=40

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

PJ64_GRID_SELFTEST=1 "$BIN" --grid "$ROM" "$ROM" "$ROM" "$ROM" >"$LOG" 2>&1 &
GRID=$!

I=0
while [ "$I" -lt "$TIMEOUT" ]; do
    COUNT=$(grep -c '^grid-selftest ' "$LOG" 2>/dev/null || true)
    [ "$COUNT" -ge "$TILES" ] && break
    sleep 1
    I=$((I + 1))
done

kill -TERM "$GRID" 2>/dev/null || true
wait "$GRID" 2>/dev/null || true

FAIL=0
REPORTS=$(grep '^grid-selftest ' "$LOG" 2>/dev/null || true)
COUNT=$(printf '%s\n' "$REPORTS" | grep -c '^grid-selftest ' || true)
PIDS=$(printf '%s\n' "$REPORTS" | sed -n 's/^grid-selftest pid=\([0-9]*\).*/\1/p' | sort -u | wc -l | tr -d ' ')

if [ "$COUNT" -lt "$TILES" ]; then
    echo "FAIL: expected $TILES tile reports, got $COUNT" >&2
    FAIL=1
fi
if [ "$PIDS" -lt "$TILES" ]; then
    echo "FAIL: expected $TILES distinct tile pids, got $PIDS" >&2
    FAIL=1
fi
if printf '%s\n' "$REPORTS" | grep '^grid-selftest ' | grep -v 'a=1 start=1' | grep -q .; then
    echo "FAIL: a tile did not receive the key pattern" >&2
    FAIL=1
fi

sleep 2
if pgrep -f "$BIN --tile" >/dev/null 2>&1; then
    echo "FAIL: tiles survived the orchestrator" >&2
    FAIL=1
fi

if [ "$FAIL" -eq 0 ]; then
    echo "ok: $COUNT tiles received the key pattern"
    rm -f "$LOG"
else
    echo "log: $LOG" >&2
fi
exit "$FAIL"
