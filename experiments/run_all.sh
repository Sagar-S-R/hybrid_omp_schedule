#!/usr/bin/env bash
# run_all.sh — full pipeline: profile → overhead → sweep → compare → analyze → guide
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
RESULTS_DIR="$REPO_ROOT/results/raw"

find_bin() {
    local name="$1"
    local p
    p=$(find "$BUILD_DIR" -name "$name" -type f 2>/dev/null | head -1)
    if [ -z "$p" ]; then echo "ERROR: $name not found in $BUILD_DIR. Build first." >&2; exit 1; fi
    echo "$p"
}

HS_BENCH=$(find_bin "hs_bench")
HS_PROFILE=$(find_bin "hs_power_profile")
HS_ANALYZE=$(find_bin "hs_analyze")
HS_GUIDE=$(find_bin "hs_decision_guide")

export OMP_PROC_BIND=false
export OMP_DYNAMIC=false

TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="$RESULTS_DIR/$TS"
mkdir -p "$OUT_DIR"

echo "============================================================"
echo " hybrid-omp-sched full experiment pipeline"
echo " Output directory: $OUT_DIR"
echo "============================================================"
echo ""
echo "CHECKLIST before proceeding:"
echo "  [ ] Plugged into mains power"
echo "  [ ] Performance governor active (run scripts/stable_mode.sh)"
echo "  [ ] RAPL readable (run scripts/rapl_permissions.sh)"
echo "  [ ] No heavy background workloads"
echo "  [ ] Consistent room temperature"
echo ""
read -r -p "Press ENTER to continue or Ctrl-C to abort..."

# ── 1. Power profile ──────────────────────────────────────────────────────────
echo ""
echo "[1/5] Running hs_power_profile..."
"$HS_PROFILE" --out "$OUT_DIR/power_profile.txt" || {
    echo "WARNING: power profiling failed (RAPL may be unavailable). Continuing with defaults."
}
export HS_POWER_PROFILE="$OUT_DIR/power_profile.txt"

# ── 2. Overhead ───────────────────────────────────────────────────────────────
echo ""
echo "[2/5] Measuring framework overhead..."
"$HS_BENCH" overhead --reps 20 --out "$OUT_DIR/overhead.csv"

# ── 3. Sweep ──────────────────────────────────────────────────────────────────
echo ""
echo "[3/5] Running sweep (this takes a while)..."
"$HS_BENCH" sweep --kernel all --size medium --reps 5 --out "$OUT_DIR/sweep.csv"

# ── 4. Compare ────────────────────────────────────────────────────────────────
echo ""
echo "[4/5] Running compare (randomised order, cooldowns)..."
"$HS_BENCH" compare --kernel all --size medium --reps 15 \
    --cooldown-ms 1000 --out "$OUT_DIR/compare.csv"

# ── 5. Analyze + guide ────────────────────────────────────────────────────────
echo ""
echo "[5/5] Analysing results..."
"$HS_ANALYZE" "$OUT_DIR/compare.csv" \
    --baseline omp_static \
    --out "$OUT_DIR/summary.md"

"$HS_GUIDE" "$OUT_DIR/sweep.csv" \
    --out "$OUT_DIR/decision_guide.md"

echo ""
echo "============================================================"
echo " Pipeline complete!"
echo " Results:        $OUT_DIR/"
echo " Summary:        $OUT_DIR/summary.md"
echo " Decision guide: $OUT_DIR/decision_guide.md"
echo "============================================================"
