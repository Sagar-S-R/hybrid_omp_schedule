# HANDOFF DOCUMENT — hybrid-omp-sched
# Read this fully before doing anything. This is the complete context.

---

## WHO YOU ARE TALKING TO
- Student: Sagar S R (1ms23cs158@msrit.edu), Ramaiah Institute of Technology, Bangalore
- Course: CS72 Multicore Architecture and Programming
- Supervisor: Dr. Mallegowda M. (Associate Professor)
- Co-author: Kushal L (1ms23cs094@msrit.edu)

---

## WHAT THIS PROJECT IS

We built a complete C++17/OpenMP scheduling library called **hybrid-omp-sched**.

### The Problem
Intel 12th-gen CPUs (like the i5-1235U in this laptop) have:
- 2 P-cores (Performance, fast, 4.4 GHz, with HyperThreading = 4 logical CPUs: 0,1,2,3)
- 8 E-cores (Efficient, slower, 3.3 GHz, no HT: CPUs 4-11)

OpenMP's default `schedule(static)` gives every thread the same number of iterations.
P-cores finish fast, then wait idle at the barrier while E-cores catch up.
This wastes time AND energy.

### Our Solution
A user-level scheduler (`hs::hybrid_for`) that for each parallel loop:
1. Runs 3% of iterations as warm-up and measures per-thread speed
2. Enumerates all (a P-threads, b E-threads) combinations
3. Picks the combination with lowest predicted EDP (Energy × Time)
4. Splits iterations proportionally to thread speed (not equally)
5. Uses 90% static + 10% dynamic tail partition

### Replacing a loop is one line:
```cpp
// Before
#pragma omp parallel for
for (size_t i = 0; i < n; ++i) body(i);

// After
hs::hybrid_for("loop.name", 0, n, [&](size_t i){ body(i); });
```

---

## WHAT HAS BEEN BUILT AND WORKS

### Code (all written, compiles, tests pass)
- `include/hybrid_sched/` — all public headers
- `src/` — library implementation (topology, energy, power model, calibrator, planner, partition, loop_state, hybrid_for)
- `benchmarks/` — hs_bench executable with 8 kernels and all variants
- `tools/` — hs_power_profile, hs_analyze, hs_decision_guide
- `tests/` — 103 unit tests, all pass
- `paper/main.tex` — IEEE two-column LaTeX paper (needs real results)
- `experiments/analysis/plots/simulate_edp.py` — EDP simulation script

### 8 Kernels implemented and verified:
1. stream_triad (memory-bound STREAM)
2. matmul (blocked GEMM)
3. mlp_train (MLP neural network training)
4. conv_layer (CNN direct convolution)
5. kmeans (K-means clustering)
6. mandelbrot (irregular compute)
7. bfs (graph BFS, irregular memory)
8. uniform_fma (synthetic, for overhead measurement)

### Test results (from WSL2):
- Build: clean, zero errors, zero warnings
- Tests: 103/103 pass
- ASan/UBSan: clean

### Timing results (from WSL2 with simulated hybrid topology):
Using HS_P_CPUS="0-3" HS_E_CPUS="4-11"

| Variant | stream_triad | matmul | Speedup |
|---------|-------------|--------|---------|
| omp_static (12T) | 93.1ms | 112.4ms | 1.00x |
| omp_static P_ONLY | 22.2ms | 48.1ms | baseline |
| hybrid_edp | 28.9ms | 44.2ms | 3.22x* |

*CI excludes 1.0 — statistically significant

### What DOESN'T work on WSL2:
- RAPL energy measurement (no /sys/class/powercap/ in WSL2)
- Hybrid sysfs auto-detection (no /sys/devices/cpu_core in WSL2)
- Thread pinning (sched_setaffinity works but has no effect in WSL2)

---

## WHAT NEEDS TO BE DONE ON NATIVE UBUNTU

This is why you are here. The laptop dual-boots Ubuntu.

### Goal
Get REAL energy measurements using RAPL on native Ubuntu.
This will give us real Energy (Joules) and EDP (Energy × Time) numbers
to put in the paper instead of model estimates.

### Step by step — do these IN ORDER:

#### Step 1 — Clone the repo
```bash
sudo apt-get update
sudo apt-get install -y git cmake build-essential g++ libomp-dev python3-matplotlib python3-numpy

git clone https://github.com/YOUR_GITHUB_USERNAME/hybrid-omp-sched.git
cd hybrid-omp-sched
```

#### Step 2 — Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHS_NATIVE=OFF \
  -DHS_BUILD_BENCH=ON -DHS_BUILD_TOOLS=ON \
  -DHS_BUILD_TESTS=ON -DHS_BUILD_EXAMPLES=ON

cmake --build build -j$(nproc)
```

#### Step 3 — Verify hybrid topology is detected
```bash
cat /sys/devices/cpu_core/cpus   # should print: 0-3
cat /sys/devices/cpu_atom/cpus   # should print: 4-11
lscpu -e                          # show full CPU layout
./build/benchmarks/hs_bench list  # should show P-slots:4, E-slots:8, Hybrid:yes
```
If /sys/devices/cpu_core/cpus does NOT exist, tell Kiro and it will help.

#### Step 4 — Enable RAPL
```bash
sudo bash scripts/rapl_permissions.sh
cat /sys/class/powercap/intel-rapl:0/energy_uj
```
This must print a large number (like 1234567890).
If it says "No such file or directory", run:
```bash
sudo modprobe intel_rapl_common
sudo modprobe intel_rapl_msr
cat /sys/class/powercap/intel-rapl:0/energy_uj
```
If still not working, tell Kiro exactly what error you see.

#### Step 5 — Set performance governor
```bash
sudo bash scripts/stable_mode.sh
```

#### Step 6 — Measure power profile
```bash
sudo ./build/tools/hs_power_profile --out power_profile.txt
export HS_POWER_PROFILE=./power_profile.txt
```

#### Step 7 — Run all benchmarks
```bash
export OMP_PROC_BIND=false
export OMP_DYNAMIC=false

./build/benchmarks/hs_bench compare \
  --kernel all --size small --reps 15 \
  --cooldown-ms 1000 \
  --out ~/results.csv

echo "DONE"
wc -l ~/results.csv
```
Expected: about 1200 rows, takes 15-20 minutes.
PASTE THE OUTPUT to Kiro.

#### Step 8 — Analyze results
```bash
./build/tools/hs_analyze ~/results.csv \
  --baseline "omp_static,c=0,t=12,aff=ORDERED" \
  --out ~/summary.md

cat ~/summary.md
```
PASTE THE OUTPUT to Kiro.

#### Step 9 — Generate figures
```bash
python3 experiments/analysis/plots/simulate_edp.py ~/results.csv ~/figures/
ls ~/figures/
```

#### Step 10 — Copy results to paper
```bash
# Copy figures to paper directory
cp ~/figures/*.pdf paper/

# Update the paper with real numbers
# Tell Kiro: "here are my real results, update the LaTeX"
```

---

## THE PAPER

File: `paper/main.tex`
- IEEE two-column format
- 4 page limit
- Authors: Dr. Mallegowda M., Sagar S R, Kushal L

### Current state of paper:
- All sections written
- Results tables have PLACEHOLDER numbers
- Figures are generated from tikz with PLACEHOLDER data
- Needs real numbers from Ubuntu measurements

### After getting real results, tell Kiro:
"Here are my real measurement results: [paste summary.md]
Update the LaTeX paper with these real numbers."

---

## KEY FILES TO KNOW

```
hybrid-omp-sched/
├── paper/main.tex                              ← THE PAPER (update with real results)
├── benchmarks/main.cpp                         ← hs_bench CLI
├── experiments/analysis/plots/simulate_edp.py ← EDP analysis script
├── scripts/rapl_permissions.sh                 ← enable RAPL
├── scripts/stable_mode.sh                      ← set performance governor
├── tools/analyze.cpp                           ← hs_analyze tool
└── HANDOFF.md                                  ← THIS FILE
```

---

## HOW TO START THE NEW KIRO CHAT

Paste this exact message at the start:

---
"I am Sagar, working on the hybrid-omp-sched project for my CS72 course at MSRIT.
I am now on native Ubuntu with an Intel Core i5-1235U (hybrid P+E core CPU).
The project is a C++17/OpenMP scheduler that minimises EDP on hybrid CPUs.
The full context is in HANDOFF.md in the repo.
I need to: (1) verify RAPL works, (2) run benchmarks, (3) get real energy numbers, (4) update the paper.
Starting from Step 3 in HANDOFF.md — here is what I see:
[paste output of: cat /sys/devices/cpu_core/cpus && cat /sys/class/powercap/intel-rapl:0/energy_uj]"
---

---

## WHAT KIRO SHOULD DO IN THE NEW CHAT

1. Verify RAPL and hybrid topology are working
2. Help fix any issues with RAPL access
3. Run the benchmark suite
4. Analyze the CSV output
5. Update paper/main.tex with real measured numbers
6. Generate final figures
7. Help compile the PDF

---

## IMPORTANT NOTES FOR NEW KIRO

- Do NOT suggest WSL2 — we are on NATIVE Ubuntu
- Do NOT make up energy numbers — only use real RAPL measurements
- The code is ALREADY WRITTEN AND WORKING — do not rewrite anything
- The paper template is at paper/main.tex — just update the numbers
- EDP = Energy × Time (lower is better)
- Our method is hybrid_edp,calib=every
- Baseline for comparison is omp_static,c=0,t=12,aff=ORDERED
- Bootstrap CI with 10000 resamples, flag * when CI excludes 1.0
- All results need mean ± 95% CI

---

## QUICK REFERENCE — VARIANT LABELS

| Label in CSV | What it means |
|-------------|---------------|
| omp_static,c=0,t=12,aff=ORDERED | Standard OpenMP, all 12 threads |
| omp_static,c=0,t=4,aff=P_ONLY | Only 4 P-core threads |
| omp_static,c=0,t=8,aff=E_ONLY | Only 8 E-core threads |
| fixed_ratio,r=2 | P:E weight 2:1, one-shot calibration |
| hybrid_time_balanced,calib=every | Our framework, minimise time |
| hybrid_edp,calib=every | OUR METHOD, minimise EDP |
| hybrid_energy,calib=every | Our framework, minimise energy only |

---

## IF SOMETHING BREAKS

### RAPL not found:
```bash
sudo modprobe intel_rapl_common intel_rapl_msr
ls /sys/class/powercap/
```

### Hybrid topology not detected:
```bash
export HS_P_CPUS="0-3"
export HS_E_CPUS="4-11"
```

### Build fails:
```bash
sudo apt-get install -y g++-12 libomp-dev
export CC=gcc-12 CXX=g++-12
cmake -S . -B build ... (same as above)
```

### Permission denied on RAPL:
```bash
sudo chmod a+r /sys/class/powercap/intel-rapl*/energy_uj
# OR just run benchmarks with sudo
sudo ./build/benchmarks/hs_bench compare ...
```
