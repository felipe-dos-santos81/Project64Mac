#!/bin/sh
# Prove the pointer path end to end: the frontend publishes an injected pointer sample, the
# plugin latches the zone under it with Config/mouse/sm64.yaml loaded, and the N64 bits
# come out right. Two runs: a click in the centre must set A; a click in top4 must set
# Start. Design: Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md
set -eu

ROM="${1:?usage: pointer_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
YAML="$ROOT/Config/mouse/sm64.yaml"
TIMEOUT=20

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

# $1 = inject spec, $2 = expected report tail. The window is 640x480.
one_run() {
    LOG="$(mktemp)"
    PJ64_INPUT_YAML="$YAML" PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$ROM" >"$LOG" 2>&1 &
    PID=$!
    I=0
    while [ "$I" -lt "$TIMEOUT" ]; do
        grep -q '^pointer-selftest ' "$LOG" 2>/dev/null && break
        sleep 1
        I=$((I + 1))
    done
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    REPORT="$(grep '^pointer-selftest ' "$LOG" || true)"
    if [ "$REPORT" = "pointer-selftest $2" ]; then
        rm -f "$LOG"
        return 0
    fi
    echo "FAIL: inject $1: got '$REPORT', wanted 'pointer-selftest $2' (log: $LOG)" >&2
    return 1
}

FAIL=0
one_run "320,240,1" "zone=12 a=1 start=0" || FAIL=1
one_run "600,20,1" "zone=3 a=0 start=1" || FAIL=1

if [ "$FAIL" -eq 0 ]; then
    echo "ok: pointer path maps centre to A and top4 to Start"
fi
exit "$FAIL"
