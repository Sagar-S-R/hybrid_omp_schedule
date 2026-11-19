#!/usr/bin/env bash
# setup_ubuntu.sh — install build dependencies on Ubuntu 22.04 / 24.04
set -euo pipefail

echo "=== hybrid-omp-sched: Ubuntu setup ==="

sudo apt-get update -qq
sudo apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    g++-12 \
    clang-14 \
    libomp-dev \
    linux-tools-common \
    linux-tools-generic \
    cpufrequtils \
    lm-sensors \
    powercap-utils 2>/dev/null || true

echo ""
echo "=== CPU layout ==="
lscpu -e 2>/dev/null || lscpu

echo ""
echo "=== Hybrid sysfs (Intel) ==="
for d in /sys/devices/cpu_core /sys/devices/cpu_atom; do
    if [ -f "$d/cpus" ]; then
        echo "$d/cpus = $(cat $d/cpus)"
    else
        echo "$d not found (may not be a hybrid CPU)"
    fi
done

echo ""
echo "=== RAPL domains ==="
ls /sys/class/powercap/ 2>/dev/null | grep intel-rapl || echo "No RAPL domains found"

echo ""
echo "Setup complete. Next steps:"
echo "  bash scripts/rapl_permissions.sh   # make RAPL readable"
echo "  bash scripts/stable_mode.sh        # set performance governor"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"
echo "  ctest --test-dir build"
