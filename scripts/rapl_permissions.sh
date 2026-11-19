#!/usr/bin/env bash
# rapl_permissions.sh — make RAPL energy_uj files readable without sudo.
# NOTE: These permissions reset on reboot. Run this script after each boot,
#       or add to /etc/rc.local, or run benchmarks with sudo.
set -euo pipefail

echo "=== RAPL permissions ==="
echo "Making energy_uj files world-readable..."

found=0
for path in /sys/class/powercap/intel-rapl*/energy_uj; do
    if [ -f "$path" ]; then
        chmod a+r "$path"
        echo "  chmod a+r $path"
        found=$((found + 1))
    fi
done

if [ "$found" -eq 0 ]; then
    echo "No RAPL energy_uj files found."
    echo "Possible reasons:"
    echo "  1. Not running on Intel hardware"
    echo "  2. RAPL module not loaded: sudo modprobe intel_rapl_common"
    echo "  3. Kernel >= 5.10 restricts RAPL by default (PLATYPUS mitigation)"
    echo "     To allow: echo 0 | sudo tee /sys/bus/event_source/devices/power/type"
    echo "     Or:       sudo sysctl -w kernel.perf_event_paranoid=0"
    echo "  Alternative: run all benchmarks with sudo"
    exit 1
fi

echo ""
echo "$found file(s) made readable."
echo "NOTE: This resets on reboot."
echo ""
echo "Verify with:"
echo "  cat /sys/class/powercap/intel-rapl:0/energy_uj"
