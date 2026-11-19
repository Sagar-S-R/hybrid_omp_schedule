#!/usr/bin/env bash
# stable_mode.sh — set performance governor and optionally disable turbo.
# Saves current state to /tmp/hs_mode_backup for restore_mode.sh.
set -euo pipefail

NO_TURBO=0
for arg in "$@"; do
    [ "$arg" = "--no-turbo" ] && NO_TURBO=1
done

echo "=== hybrid-omp-sched: stable measurement mode ==="

# ── Save current state ────────────────────────────────────────────────────────
BACKUP=/tmp/hs_mode_backup
mkdir -p "$BACKUP"

# Governor for cpu0
CURRENT_GOV=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")
echo "$CURRENT_GOV" > "$BACKUP/governor"
echo "  Saved governor: $CURRENT_GOV"

# Turbo state
NO_TURBO_FILE=/sys/devices/system/cpu/intel_pstate/no_turbo
if [ -f "$NO_TURBO_FILE" ]; then
    cat "$NO_TURBO_FILE" > "$BACKUP/no_turbo"
    echo "  Saved no_turbo: $(cat $NO_TURBO_FILE)"
fi

# ── Set performance governor ──────────────────────────────────────────────────
N_CPUS=$(nproc)
for i in $(seq 0 $((N_CPUS - 1))); do
    GOV_FILE=/sys/devices/system/cpu/cpu${i}/cpufreq/scaling_governor
    if [ -f "$GOV_FILE" ]; then
        echo performance | sudo tee "$GOV_FILE" > /dev/null
    fi
done
echo "  Set governor: performance on all $N_CPUS CPUs"

# ── Optionally disable turbo ──────────────────────────────────────────────────
if [ "$NO_TURBO" -eq 1 ] && [ -f "$NO_TURBO_FILE" ]; then
    echo 1 | sudo tee "$NO_TURBO_FILE" > /dev/null
    echo "  Turbo: DISABLED (no_turbo=1)"
else
    echo "  Turbo: unchanged (pass --no-turbo to disable)"
fi

echo ""
echo "=== Current state ==="
echo "  Governor:  $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo N/A)"
[ -f "$NO_TURBO_FILE" ] && echo "  no_turbo:  $(cat $NO_TURBO_FILE)"
echo ""
echo "REMINDER before running benchmarks:"
echo "  - Plug in power (do not run on battery)"
echo "  - Keep performance/power mode in BIOS/firmware"
echo "  - Close unnecessary applications"
echo "  - Keep lid open and room temperature consistent"
echo "  - Do not mix runs from different thermal/power states"
echo ""
echo "Run restore_mode.sh to revert when done."
