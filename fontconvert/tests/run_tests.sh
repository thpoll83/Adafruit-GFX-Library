#!/usr/bin/env bash
# Run all fontconvert Python tests and list the PNG contact sheets produced.
#
# Usage:
#   ./run_tests.sh                    # all tests
#   ./run_tests.sh -k TestCommaSeq   # filter by name (any pytest flag works)
#   ./run_tests.sh -x                 # stop on first failure

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/../cmake-build-debug/fontconvert"
OUT_DIR="$SCRIPT_DIR/font_test_output"

# ── header ────────────────────────────────────────────────────────────────────
echo "╔══════════════════════════════════════════════════════════════════════╗"
echo "║                    fontconvert test suite                           ║"
echo "╚══════════════════════════════════════════════════════════════════════╝"
echo ""
echo "  Tests dir : $SCRIPT_DIR"

if [ -x "$BINARY" ]; then
    # Run binary (exits 1 on no args); absorb non-zero so pipefail doesn't fire.
    VERSION=$( { "$BINARY" 2>&1; true; } | grep -oP 'FreeType Version \K[0-9.]+' | head -1)
    [ -n "$VERSION" ] || VERSION="unknown"
    echo "  Binary    : $BINARY  (FreeType $VERSION)"
else
    echo "  Binary    : NOT FOUND at $BINARY"
    echo "  → build first:  cd fontconvert/cmake-build-debug && cmake --build ."
fi
echo ""

# ── run pytest ────────────────────────────────────────────────────────────────
cd "$SCRIPT_DIR"
python3 -m pytest -v "$@"
PYTEST_EXIT=$?

# ── PNG summary ───────────────────────────────────────────────────────────────
echo ""
echo "─── PNG contact sheets ──────────────────────────────────────────────────"
if [ -d "$OUT_DIR" ]; then
    total=0
    total_bytes=0
    while IFS= read -r f; do
        bytes=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null || echo 0)
        printf "  %-68s %6d B\n" "$(basename "$f")" "$bytes"
        total=$(( total + 1 ))
        total_bytes=$(( total_bytes + bytes ))
    done < <(find "$OUT_DIR" -name "*.png" | sort)
    echo ""
    printf "  %d PNG(s)  —  %.1f KB total  →  %s\n" \
        "$total" "$(echo "scale=1; $total_bytes / 1024" | bc)" "$OUT_DIR"
else
    echo "  (font_test_output/ not yet created — run tests first)"
fi
echo ""

exit $PYTEST_EXIT
