# Setup Guide

## Target Machine: Intel Core i5-1235U (Ubuntu 24.04)

### Step 1 — Verify Hybrid CPU

```bash
# Confirm P-cores and E-cores are visible
ls /sys/devices/cpu_core /sys/devices/cpu_atom
cat /sys/devices/cpu_core/cpus   # should print something like 0-3
cat /sys/devices/cpu_atom/cpus   # should print something like 4-11

# Full CPU layout
lscpu -e
# Look for: MAXMHZ column — P-cores run at ~4.4 GHz, E-cores at ~3.3 GHz
```

### Step 2 — Install Dependencies

```bash
bash scripts/setup_ubuntu.sh
```

This installs: `build-essential cmake ninja-build git g++-12 clang-14 libomp-dev linux-tools-* cpufrequtils lm-sensors`

### Step 3 — Enable RAPL

```bash
bash scripts/rapl_permissions.sh

# Verify:
cat /sys/class/powercap/intel-rapl:0/energy_uj
# Should print a large integer (e.g. 1234567890). If you get "Permission denied", rerun with sudo.
```

### Step 4 — Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

For portable/CI build (no `-march=native`):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHS_NATIVE=OFF
```

With sanitizers (debug mode):
```bash
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug -DHS_NATIVE=OFF -DHS_SANITIZE=ON
cmake --build build-san -j$(nproc)
```

Note: TSan requires a separate build — it cannot be combined with ASan:
```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DHS_NATIVE=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread"
```

### Step 5 — Run Tests

```bash
# With homogeneous fallback (works on any machine)
HS_FORCE_HOMOGENEOUS=1 OMP_PROC_BIND=false OMP_NUM_THREADS=2 \
  ctest --test-dir build --output-on-failure

# On the actual hybrid machine (uses real topology):
OMP_PROC_BIND=false ctest --test-dir build --output-on-failure
```

### Step 6 — Quick Sanity Check

```bash
# List topology and RAPL status
./build/hs_bench list

# Run one kernel, verify result
OMP_PROC_BIND=false ./build/hs_bench run \
  --kernel stream_triad --variant hybrid_edp --size small \
  --reps 3 --verify

# Inspect per-call plan
OMP_PROC_BIND=false ./build/03_inspect_plan
```

### Step 7 — Set Stable Mode and Run Experiments

```bash
# Set performance governor
sudo bash scripts/stable_mode.sh

# Measure power profile (needed for non-default power model)
sudo ./build/hs_power_profile --out power_profile.txt

# Full experiment pipeline
export HS_POWER_PROFILE=./power_profile.txt
bash experiments/run_all.sh
```

---

## WSL2 / Windows Note

WSL2 does **not** expose RAPL, Intel hybrid sysfs, or `sched_setaffinity`. The library will:
- Use a homogeneous fallback topology (all CPUs treated as P-cores)
- Return NaN for all energy measurements
- Disable thread pinning (silently)
- Compile and pass all unit tests

**Do not run or publish benchmarks from WSL2** — the measurements are not meaningful.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `hs_bench list` shows 0 E-cores | Running on non-hybrid or WSL2 | Set `HS_P_CPUS` / `HS_E_CPUS` manually, or use native Linux |
| `energy_j_gross = NaN` everywhere | RAPL not readable | Run `scripts/rapl_permissions.sh` |
| Tests fail with "OMP_PROC_BIND" warning | libgomp fighting our pinning | Set `OMP_PROC_BIND=false` |
| Build error: `fopenmp` not found | OpenMP not installed | `sudo apt install libomp-dev` |
| `sched_setaffinity` fails | Running in container/WSL | Set `HS_DISABLE_PINNING=1` |
| Very high imbalance | Thermal throttling | Check `temp_c_before/after`; let machine cool |
