#!/usr/bin/env bash
# restore_mode.sh — revert governor and turbo to saved state.
set -euo pipefail

BACKUP=/tmp/hs_mode_backup
if [ ! -d "$BACKUP" ]; then
    echo "No backup found at $BACKUP. Nothing to restore."
    exit 0
fi

echo "=== hybrid-omp-sched: restoring system state ==="

N_CPUS=$(nproc)

# Restore governor
if [ -f "$BACKUP/governor" ]; then
    GOV=$(cat "$BACKUP/governor")
    for i in $(seq 0 $((N_CPUS - 1))); do
        GOV_FILE=/sys/devices/system/cpu/cpu${i}/cpufreq/scaling_governor
        [ -f "$GOV_FILE" ] && echo "$GOV" | sudo tee "$GOV_FILE" > /dev/null
    done
    echo "  Governor restored to: $GOV"
fi

# Restore turbo
NO_TURBO_FILE=/sys/devices/system/cpu/intel_pstate/no_turbo
if [ -f "$BACKUP/no_turbo" ] && [ -f "$NO_TURBO_FILE" ]; then
    VAL=$(cat "$BACKUP/no_turbo")
    echo "$VAL" | sudo tee "$NO_TURBO_FILE" > /dev/null
    echo "  no_turbo restored to: $VAL"
fi

echo "Done."
