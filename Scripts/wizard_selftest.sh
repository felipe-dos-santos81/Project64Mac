#!/bin/sh
# Drives the binding wizard's screens with synthetic events and checks the YAML it writes.
# No window, no ROM and no camera: --selftest never starts the tracker.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64-wizard"
OUT=$(mktemp /tmp/pj64-wizard-selftest-XXXXXX)
EDIR=$(mktemp -d /tmp/pj64-edit-selftest-XXXXXX)
trap 'rm -f "$OUT" "$OUT.expected" "$OUT.got"; rm -rf "$EDIR"' EXIT

[ -x "$BIN" ] || { echo "not built: $BIN" >&2; exit 1; }

PJ64_FACE=0 "$BIN" --selftest "$OUT" || { echo "selftest exited non-zero" >&2; exit 1; }

cat > "$OUT.expected" <<'YAML'
bindings:
  A:         {key: X}
  B:         {button: a}
  Z:         {zone: mid1}
  Start:     {face: mouth-open}
  Stick:     {stick: head-digital}
YAML

# Compare the bindings block only: the header names the base and is prose.
sed -n '/^bindings:/,$p' "$OUT" > "$OUT.got"
if ! diff -u "$OUT.expected" "$OUT.got"; then
    echo "wizard-selftest: the emitted bindings differ from what the canned run should produce" >&2
    exit 1
fi
echo "ok: wizard selftest"

# The panel editor: --edit with PJ64_EDIT_SELFTEST=1 drives the real editor screen with scripted
# clicks and no window (main.cpp's EditScript), starting from the generic layout because the
# ROM has none of its own. Three runs: the first writes the layout and no .orig; the second
# saves over it and keeps it as .orig; the third must leave that .orig alone.
: > "$EDIR/game.z64"
edit_run() {
    PJ64_FACE=0 PJ64_EDIT_SELFTEST=1 perl -e 'alarm 30; exec @ARGV' -- "$BIN" --edit "$EDIR/game.z64" \
        2>"$EDIR/log" || { cat "$EDIR/log" >&2; echo "wizard-selftest: --edit exited non-zero" >&2; exit 1; }
}
edit_run
[ ! -e "$EDIR/game.yaml.orig" ] || { echo "wizard-selftest: the first save made a .orig" >&2; exit 1; }
cat > "$OUT.expected" <<'YAML'
bindings:
  B:         {zone: mid3}
  Z:         {zone: game}
  Start:     {zone: mid1}
  L:         {zone: mid4, toggle: true}
  R:         {zone: pad-down}
  CUp:       {zone: c-up}
  CDown:     {zone: c-down}
  CLeft:     {zone: c-left}
  CRight:    {zone: c-right}
  DPadLeft:  {zone: pad-left}
  DPadRight: {zone: pad-right}
  Stick:     {stick: pointer, hold: pad-up}
  Menu:      {zone: mid5}
YAML
sed -n '/^bindings:/,$p' "$EDIR/game.yaml" > "$OUT.got"
if ! diff -u "$OUT.expected" "$OUT.got"; then
    echo "wizard-selftest: the editor's layout differs from what the scripted clicks should produce" >&2
    exit 1
fi
cp "$EDIR/game.yaml" "$EDIR/first.yaml"
edit_run
cmp -s "$EDIR/game.yaml.orig" "$EDIR/first.yaml" || { echo "wizard-selftest: the second save did not keep the first as .orig" >&2; exit 1; }
edit_run
cmp -s "$EDIR/game.yaml.orig" "$EDIR/first.yaml" || { echo "wizard-selftest: a third save touched the .orig" >&2; exit 1; }
[ ! -e "$EDIR/game.yaml.tmp" ] || { echo "wizard-selftest: a temporary file was left behind" >&2; exit 1; }
echo "ok: wizard edit selftest"
