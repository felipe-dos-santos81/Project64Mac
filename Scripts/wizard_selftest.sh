#!/bin/sh
# Drives the binding wizard's screens with synthetic events and checks the YAML it writes.
# No window, no ROM and no camera: --selftest never starts the tracker.
set -eu

BIN=Bin/macOS/Project64-wizard
OUT=$(mktemp /tmp/pj64-wizard-selftest-XXXXXX)
trap 'rm -f "$OUT" "$OUT.expected" "$OUT.got"' EXIT

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
