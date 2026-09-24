#!/bin/sh
# Renders the wizard's screenshot tour into a temporary directory and compares every file
# byte for byte with the committed copies under Docs/img/wizard. The software renderer is
# deterministic, so a difference means a screen changed, or the Homebrew SDL build did:
# either way, `make wizard-screenshots` and commit the result.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/Bin/macOS/Project64-wizard"
DOCS="$ROOT/Docs/img/wizard"
TMP=$(mktemp -d /tmp/pj64-wizard-shots-XXXXXX)
trap 'rm -rf "$TMP"' EXIT

[ -x "$BIN" ] || { echo "not built: $BIN" >&2; exit 1; }

# The fourteen stops Docs/UserGuide.md references. A stop added to the tour is added here and
# to the guide; one dropped from the tour fails here until it is dropped from both.
STOPS="01-base.png 02-control.png 03-key-armed.png 04-key-bound.png 05-no-gamepad.png \
06-zone.png 07-gestures.png 08-stick-forms.png 09-review.png 10-save.png 11-save-warning.png \
12-edit-panel.png 13-edit-chooser.png 14-edit-gestures.png"

# alarm: a binary without the flag would open its window and wait forever.
PJ64_FACE=0 perl -e 'alarm 60; exec @ARGV' -- "$BIN" --screenshots "$TMP" >/dev/null \
    || { echo "wizard-screenshots-check: --screenshots failed" >&2; exit 1; }

for f in $STOPS; do
    [ -f "$TMP/$f" ] || { echo "wizard-screenshots-check: the tour did not write $f" >&2; exit 1; }
    [ -f "$DOCS/$f" ] || { echo "wizard-screenshots-check: $f is not committed; run make wizard-screenshots and commit Docs/img/wizard" >&2; exit 1; }
    cmp -s "$TMP/$f" "$DOCS/$f" || { echo "wizard-screenshots-check: $f differs from the committed copy; run make wizard-screenshots and commit the result" >&2; exit 1; }
done
for f in "$TMP"/*.png "$DOCS"/*.png; do
    case " $STOPS " in
        *" $(basename "$f") "*) ;;
        *) echo "wizard-screenshots-check: $f is not in the stop list; make wizard-screenshots will not remove it, so git rm the stray file, or add it to the stop list, the tour (Screenshots.cpp) and the guide" >&2; exit 1 ;;
    esac
done
echo "ok: wizard screenshots"
