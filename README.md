# hybrid-omp-sched

**Core-Type-Aware OpenMP Loop Scheduling on Hybrid P-Core/E-Core CPUs: An Energy-Delay-Product Study**

*CS72 Multicore Architecture and Programming — Sagar*

---

## The Problem

```
Standard OpenMP schedule(static) on a hybrid CPU:

P-core 0  [========work========][---waiting at barrier---]
P-core 1  [========work========][---waiting at barrier---]
E-core 0  [============slower work==========][---wait---]
E-core 1  [=============slower work===================]--]
E-core 2  [==============slower work====================]
                                              ^
                              P-cores idle here, wasting time + energy
```

Modern Intel CPUs (12th gen+) mix fast **P-cores** and efficient **E-cores**.
OpenMP's static schedule gives every thread the same iteration count, so
P-cores finish early and spin at the barrier while E-cores catch up.

---

## Our Approach

A **user-level** scheduling layer on top of stock OpenMP that, for every loop:

1. Runs a **warm-up slice** (≈3% of iterations) on all threads and measures per-thread speed
2. **Enumerates** all (a P-threads, b E-threads) configurations and predicts time, power, and EDP
3. **Picks the configuration** minimising predicted EDP — which may mean leaving E-cores idle
4. Executes the rest with a **weighted-static + small dynamic tail** partition
5. **Re-calibrates on the next call**, tracking thermal throttling and instruction-mix changes

**Replacing a loop is a one-line change:**

```cpp
// Before
#pragma omp parallel for
for (std::size_t i = 0; i < n; ++i) body(i);

// After
hs::hybrid_for("my.loop.name", 0, n, [&](std::size_t i) { body(i); });
```

---

## Quick Start

```bash
# 1. Install dependencies (Ubuntu 22.04 / 24.04)
bash scripts/setup_ubuntu.sh

# 2. Enable RAPL energy measurement
bash scripts/rapl_permissions.sh

# 3. Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 4. Run tests
HS_FORCE_HOMOGENEOUS=1 OMP_PROC_BIND=false OMP_NUM_THREADS=2 \
  ctest --test-dir build --output-on-failure

# 5. Inspect topology and run an example
OMP_PROC_BIND=false ./build/hs_bench list
OMP_PROC_BIND=false ./build/hs_bench run \
  --kernel stream_triad --variant hybrid_edp --size small --reps 5 --verify

# 6. Full experiment pipeline (hybrid machine + RAPL required)
sudo bash scripts/stable_mode.sh
bash experiments/run_all.sh
```

---

## How the Algorithm Works

1. **Detect topology** — P-cores vs E-cores via Intel hybrid sysfs, frequency heuristic, or env override
2. **Warm up on real work** — a small slice of the actual loop body; no wasted iterations
3. **Enumerate candidate configs** — all (a, b) pairs; 44 candidates for 2P(HT)+8E; takes microseconds
4. **Predict EDP** — linear power model × predicted time²; scale factor `κ` adapts online via RAPL
5. **Partition and execute** — weighted-static blocks (largest-remainder) + dynamic tail for stragglers
6. **Refine online** — per-loop `κ` tracks how the actual power differs from the model

---

## Repository Map

```
include/hybrid_sched/   Public headers (hybrid_for, topology, energy, planner, …)
src/                    Library implementation
benchmarks/             hs_bench executable (kernels, executors, suites, CLI)
tools/                  hs_power_profile, hs_analyze, hs_decision_guide
tests/                  Unit tests (minitest framework, no external deps)
examples/               Three self-contained usage examples
experiments/            Shell scripts for the full experiment pipeline
scripts/                Setup, RAPL permissions, governor control
results/                Raw CSV output (gitignored) + sample schema
docs/                   Design, methodology, setup, API, results template
```

---

## Known Limitations

- **Linux-only measurement**: RAPL, hybrid sysfs, and `sched_setaffinity` are Linux-specific. On other platforms the library compiles and runs but uses a homogeneous fallback with no energy measurement. Do not trust benchmarks from WSL2 or macOS.
- **Requires loop wrapping**: every loop must be changed from `#pragma omp parallel for` to `hs::hybrid_for`. No automatic source transformation is provided.
- **Package-level RAPL only**: energy covers the full package (CPU cores + IGP + uncore). Per-core or per-cluster energy is not measurable with this API.
- **Short loops are not worth it**: loops predicted to run in < 50 µs execute serially. The break-even for parallel overhead is approximately 1–5 ms (measured by `hs_bench overhead`).
- **Power model is linear and approximate**: the model ignores PL1/PL2 saturation, memory-bandwidth contention, and thermal interdependence. Run `hs_power_profile` to fit machine-specific coefficients; default coefficients are rough and results with them are not publishable.
- **Conservative throughput estimation**: warm-up speeds are measured with all K slots active; subset predictions inherit this contention and may underestimate actual subset speed.
- **Not a patched runtime**: this is a user-level layer. It adds fork/join overhead relative to a single OpenMP `parallel for`. The overhead suite quantifies this.

---

## Reproducibility Checklist

Before reporting results:

- [ ] Run on the native hybrid Linux machine (not WSL2, not a VM)
- [ ] RAPL `energy_uj` files are readable (`cat /sys/class/powercap/intel-rapl:0/energy_uj` succeeds)
- [ ] Governor set to `performance` (`scripts/stable_mode.sh`)
- [ ] Power profile measured with `hs_power_profile` (not using defaults)
- [ ] Machine is plugged in to mains power
- [ ] ≥ 10 reps per variant, randomised order, cooldowns between runs
- [ ] `temp_c_before` / `temp_c_after` recorded; no runs with > 10°C rise
- [ ] Tests pass: `ctest --test-dir build --output-on-failure`
- [ ] All energy columns are non-NaN

---

## Related Work (no fabricated numbers)

Among prior works surveyed:
- **AID** (Rountree et al.): patches the OpenMP runtime to balance finish time
- **PMCSched** / **E-Mapper**: OS-level approaches using PMU events
- **Beserra et al.**: user-level speed-ratio calibration, time-balance objective, single calibration
- **Luo et al.**: thread-count tuning by EDP on homogeneous CPUs (Aurora-style)
- **Intel oneMKL guidance**: manual BLAS thread-count selection for hybrid chips

Our approach differs in targeting EDP (not just time), recalibrating per call, and running entirely at user level without modifying the OpenMP runtime or the kernel.

---

## License

MIT — see `LICENSE`. Author: Sagar.
