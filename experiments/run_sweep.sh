#!/usr/bin/env bash
# run_sweep.sh — run the full schedule × affinity sweep for all kernels.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
RESULTS_DIR="$REPO_ROOT/results/raw"

# ── Checks ────────────────────────────────────────────────────────────────────
if [ ! -f "$BUILD_DIR/hs_bench" ] && [ ! -f "$BUILD_DIR/benchmarks/hs_bench" ]; then
    echo "ERROR: hs_bench not found. Build first:"
    echo "  cmake -S $REPO_ROOT -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release"
    echo "  cmake --build $BUILD_DIR -j"
    exit 1
fi

HS_BENCH=$(find "$BUILD_DIR" -name "hs_bench" -type f | head -1)

# ── Environment ───────────────────────────────────────────────────────────────
export OMP_PROC_BIND=false
export OMP_DYNAMIC=false

# ── Hybrid check ─────────────────────────────────────────────────────────────
if [ ! -d /sys/devices/cpu_atom ] && [ -z "${HS_P_CPUS:-}" ]; then
    echo "WARNING: No hybrid CPU detected (/sys/devices/cpu_atom missing)."
    echo "         Sweep will run but P/E comparisons are not meaningful."
fi

# ── Timestamp directory ───────────────────────────────────────────────────────
TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="$RESULTS_DIR/$TS"
mkdir -p "$OUT_DIR"
echo "Results directory: $OUT_DIR"

echo ""
echo "============================================================"
echo " REMINDER: plugged in, performance mode, nothing else running"
echo "============================================================"
echo ""

"$HS_BENCH" sweep --kernel all --size medium --reps 5 --out "$OUT_DIR/sweep.csv"

echo ""
echo "Sweep complete. Analyse with:"
echo "  $BUILD_DIR/hs_analyze $OUT_DIR/sweep.csv --out $OUT_DIR/summary.md"
