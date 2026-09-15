#!/bin/sh
# Prove the pointer path end to end: the frontend publishes an injected pointer sample, the
# plugin latches the zone under it with Config/mouse/sm64.yaml loaded, and the N64 bits
# come out right. Three runs: a click in the centre must set A; a click in top4 must set
# Start; and with no PJ64_INPUT_YAML at all, a copy of the layout named after the ROM and
# sitting beside it must be found by the frontend's own lookup (centre must set A again).
# Design: Docs/superpowers/specs/2026-09-15-mouse-and-face-input-design.md and
# Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md
set -eu

ROM="${1:?usage: pointer_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
YAML="$ROOT/Config/mouse/sm64.yaml"
TIMEOUT=20

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

# The third run must see the frontend's own lookup, not a value inherited from the caller.
unset PJ64_INPUT_YAML

# $1 = inject spec, $2 = expected report tail, $3 = ROM path, $4 = PJ64_INPUT_YAML value,
# or "" to leave it unset. PJ64_FACE=0 keeps the camera prompt out of a test. The window
# is 640x480.
one_run() {
    LOG="$(mktemp)"
    if [ -n "$4" ]; then
        PJ64_INPUT_YAML="$4" PJ64_FACE=0 PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$3" >"$LOG" 2>&1 &
    else
        PJ64_FACE=0 PJ64_POINTER_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$3" >"$LOG" 2>&1 &
    fi
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
one_run "320,240,1" "zone=12 a=1 start=0" "$ROM" "$YAML" || FAIL=1
one_run "600,20,1" "zone=3 a=0 start=1" "$ROM" "$YAML" || FAIL=1

# Third run: symlink the ROM into a temp folder under its own name and extension, with the
# layout beside it as <base>.yaml. Without the lookup the keyboard mapping loads and a
# centre click cannot set A, so a=1 here proves the sibling file was used.
TMP="$(mktemp -d)"
NAME="$(basename "$ROM")"
BASE="${NAME%.*}"
ln -s "$(cd "$(dirname "$ROM")" && pwd)/$NAME" "$TMP/$NAME"
cp "$YAML" "$TMP/$BASE.yaml"
one_run "320,240,1" "zone=12 a=1 start=0" "$TMP/$NAME" "" || FAIL=1
rm -rf "$TMP"

if [ "$FAIL" -eq 0 ]; then
    echo "ok: pointer path maps centre to A and top4 to Start, and finds a layout named after the ROM"
fi
exit "$FAIL"
