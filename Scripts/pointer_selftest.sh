#!/bin/sh
# Prove the pointer path end to end: the frontend publishes an injected pointer sample, the
# plugin latches the zone under it with Config/mouse/super_mario_64_usa.yaml loaded, and the
# N64 bits come out right. Three runs: a click in the game image must set A; a click in the
# middle of mid1 (192,512, a point that only exists when the window is 640 tall, so this
# also proves the window grew and that the frontend and the plugin agree about where mid1
# is) must set Start; and with no PJ64_INPUT_YAML at all, a copy of the layout named after
# the ROM and sitting beside it must be found by the frontend's own lookup (the game click
# must set A again).
# A fourth run loads a small layout with Z as a toggle on mid2: one static press over mid2 is
# one press edge, so Z must be on and nothing latched (zone=-1).
# A fifth run presses the shipped Super Mario 64 layout's menu slot (pad-down) from the first
# frame, while the game is still booting. The menu host must report that the overlay drew at
# least two frames after the menu opened ("menu: drawn", so one of them certainly drew the
# menu) and that the core then confirmed the pause ("menu: paused"); a draw that timed out,
# or a pause the core never confirmed, fails the run even though the host carries on.
# Design: Docs/superpowers/specs/2026-09-15-mouse-panel-design.md and
# Docs/superpowers/specs/2026-09-15-per-game-input-yaml-design.md
set -eu

ROM="${1:?usage: pointer_selftest.sh <rom>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64"
YAML="$ROOT/Config/mouse/super_mario_64_usa.yaml"
TIMEOUT=20

[ -x "$BIN" ] || { echo "frontend not built: $BIN" >&2; exit 1; }

# The third run must see the frontend's own lookup, not a value inherited from the caller.
unset PJ64_INPUT_YAML

# $1 = inject spec, $2 = expected report tail, $3 = ROM path, $4 = PJ64_INPUT_YAML value,
# or "" to leave it unset. PJ64_FACE=0 keeps the camera prompt out of a test. The window
# is 640x640 with a mouse layout: game image 640x480, panel below it.
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

# $1 = inject spec, $2 = ROM path, $3 = PJ64_INPUT_YAML value. Passes when the menu host
# reports "menu: drawn" and "menu: paused", and neither "menu: draw timed out" nor
# "menu: the game did not pause" (PJ64_MENU_SELFTEST prints each phase).
menu_run() {
    LOG="$(mktemp)"
    PJ64_INPUT_YAML="$3" PJ64_FACE=0 PJ64_MENU_SELFTEST=1 PJ64_POINTER_INJECT="$1" "$BIN" "$2" >"$LOG" 2>&1 &
    PID=$!
    I=0
    while [ "$I" -lt "$TIMEOUT" ]; do
        grep -q '^menu: paused' "$LOG" 2>/dev/null && break
        sleep 1
        I=$((I + 1))
    done
    kill -TERM "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    WHY=""
    grep -q '^menu: draw timed out' "$LOG" && WHY="the overlay never drew the menu (draw timed out)"
    [ -z "$WHY" ] && grep -q '^menu: the game did not pause' "$LOG" && WHY="the core never confirmed the pause"
    [ -z "$WHY" ] && ! grep -q '^menu: drawn' "$LOG" && WHY="the menu never reported drawn"
    [ -z "$WHY" ] && ! grep -q '^menu: paused' "$LOG" && WHY="the menu never reported paused"
    if [ -z "$WHY" ]; then
        rm -f "$LOG"
        return 0
    fi
    echo "FAIL: inject $1: $WHY (log: $LOG)" >&2
    return 1
}

FAIL=0
one_run "320,240,1" "zone=13 a=1 start=0 z=0 x=0 y=0" "$ROM" "$YAML" || FAIL=1
one_run "192,512,1" "zone=8 a=0 start=1 z=0 x=0 y=0" "$ROM" "$YAML" || FAIL=1  # mid1's centre at 640x640

# Third run: symlink the ROM into a temp folder under its own name and extension, with the
# layout beside it as <base>.yaml. Without the lookup the keyboard mapping loads and a
# click in the game image cannot set A, so a=1 here proves the sibling file was used.
TMP="$(mktemp -d)"
NAME="$(basename "$ROM")"
BASE="${NAME%.*}"
ln -s "$(cd "$(dirname "$ROM")" && pwd)/$NAME" "$TMP/$NAME"
cp "$YAML" "$TMP/$BASE.yaml"
one_run "320,240,1" "zone=13 a=1 start=0 z=0 x=0 y=0" "$TMP/$NAME" "" || FAIL=1
rm -rf "$TMP"

# Fourth run: a toggle slot turns its control on without latching the press.
TOGGLE_YAML="$(mktemp /tmp/pj64-toggle-XXXXXX)"
cat >"$TOGGLE_YAML" <<'EOF'
bindings:
  Stick: {stick: pointer}
  A:     {zone: game}
  Z:     {zone: mid2, toggle: true}
EOF
one_run "256,512,1" "zone=-1 a=0 start=0 z=1 x=0 y=0" "$ROM" "$TOGGLE_YAML" || FAIL=1  # mid2's centre
rm -f "$TOGGLE_YAML"

# Fifth run: the menu slot opens the menu and the game pauses.
menu_run "80,608,1" "$ROM" "$YAML" || FAIL=1   # pad-down's centre at 640x640

if [ "$FAIL" -eq 0 ]; then
    echo "ok: pointer path maps a game click to A and mid1 to Start, finds a layout named after the ROM, toggles Z on mid2, and pauses from the menu"
fi
exit "$FAIL"
