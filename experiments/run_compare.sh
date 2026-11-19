#!/usr/bin/env bash
# run_compare.sh — run all variants in randomised order for comparison.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
RESULTS_DIR="$REPO_ROOT/results/raw"

HS_BENCH=$(find "$BUILD_DIR" -name "hs_bench" -type f 2>/dev/null | head -1)
if [ -z "$HS_BENCH" ]; then
    echo "ERROR: hs_bench not found in $BUILD_DIR. Build first."; exit 1
fi

export OMP_PROC_BIND=false
export OMP_DYNAMIC=false

TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="$RESULTS_DIR/$TS"
mkdir -p "$OUT_DIR"

echo "Results: $OUT_DIR"
echo ""
echo "============================================================"
echo " REMINDER: plugged in, stable mode, RAPL readable"
echo "============================================================"
echo ""

"$HS_BENCH" compare --kernel all --size medium --reps 15 \
    --cooldown-ms 1000 --out "$OUT_DIR/compare.csv"

echo ""
echo "Compare done. Analyse with:"
echo "  $BUILD_DIR/hs_analyze $OUT_DIR/compare.csv --baseline omp_static --out $OUT_DIR/summary.md"
