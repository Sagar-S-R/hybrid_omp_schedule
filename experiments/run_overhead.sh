#!/usr/bin/env bash
# run_overhead.sh — measure framework overhead vs loop duration.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
RESULTS_DIR="$REPO_ROOT/results/raw"

HS_BENCH=$(find "$BUILD_DIR" -name "hs_bench" -type f 2>/dev/null | head -1)
if [ -z "$HS_BENCH" ]; then
    echo "ERROR: hs_bench not found. Build first."; exit 1
fi

export OMP_PROC_BIND=false
export OMP_DYNAMIC=false

TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="$RESULTS_DIR/$TS"
mkdir -p "$OUT_DIR"

"$HS_BENCH" overhead --reps 30 --out "$OUT_DIR/overhead.csv"

echo "Overhead results: $OUT_DIR/overhead.csv"
echo "Target: hybrid overhead < 5% for loops >= 5 ms."
