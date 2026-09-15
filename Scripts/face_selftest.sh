#!/bin/sh
# Prove the face path end to end without a camera: the frontend publishes an injected
# gesture and head stick, the plugin evaluates them against
# Config/face/super_mario_64_usa.yaml, and the N64 bits and axes come out right. Two runs:
# mouth-open must set A; eyebrows with a full right tilt must set Z, not A, and put 80 on
# the X axis, which proves the head stick reached the mapped output. PJ64_FACE=0 is set as
# well: the injection never opens the camera, and neither may this script.
# Design: Docs/superpowers/specs/2026-09-15-face-expressions-design.md
set -eu

ROM="${1:?usage: face_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
YAML="$ROOT/Config/face/super_mario_64_usa.yaml"
TIMEOUT=20

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

# $1 = face inject spec, $2 = expected report tail. The pointer is pinned to the window's
# corner with no button so the real mouse cannot trip the report early.
one_run() {
    LOG="$(mktemp)"
    PJ64_INPUT_YAML="$YAML" PJ64_FACE=0 PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="0,0,0" \
        PJ64_FACE_INJECT="$1" "$BIN" "$ROM" >"$LOG" 2>&1 &
    PID=$!
    I=0
    while [ "$I" -lt "$TIMEOUT" ]; do
        grep -q '^pointer-selftest ' "$LOG" 2>/dev/null && break
        sleep 1
        I=$((I + 1))
    done
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    if grep -q '^face: tracking started' "$LOG"; then
        echo "FAIL: inject $1 opened the camera (log: $LOG)" >&2
        return 1
    fi
    REPORT="$(grep '^pointer-selftest ' "$LOG" || true)"
    if [ "$REPORT" = "pointer-selftest $2" ]; then
        rm -f "$LOG"
        return 0
    fi
    echo "FAIL: inject $1: got '$REPORT', wanted 'pointer-selftest $2' (log: $LOG)" >&2
    return 1
}

FAIL=0
one_run "mouth-open" "zone=-1 a=1 start=0 z=0 x=0 y=0" || FAIL=1
one_run "eyebrows,80,0" "zone=-1 a=0 start=0 z=1 x=80 y=0" || FAIL=1

if [ "$FAIL" -eq 0 ]; then
    echo "ok: face path maps mouth-open to A, eyebrows to Z, and the head stick to the X axis"
fi
exit "$FAIL"
