# Design

## Algorithm (Section 6 of the master spec)

### 6.1 Worker Slots and Candidate Configurations

The topology layer classifies every logical CPU as P-core or E-core.  
**P-slot order**: primary SMT threads first, then siblings.  
**E-slot order**: ascending CPU id.

A **candidate config** `(a, b)` uses the first `a` P-slots and first `b` E-slots, with `a ∈ [0, nP]`, `b ∈ [0, nE]`, `a + b ≥ 1`. For the i5-1235U (4P + 8E) this gives 44 candidates; enumeration takes microseconds.

### 6.2 One Call of `hybrid_for_range` (Policy::EDP)

```
N = end - begin

1. Lookup LoopState for loop_name
   → decide need_calibration (EVERY_CALL / EVERY_N / ON_DRIFT / ONCE)

2. Early exits
   N < 2 * max(grain, K)  → serial
   omp_in_parallel()      → serial in calling thread

3. Warm-up (if need_calibration)
   W = clamp(ceil(warmup_fraction * N), min_warmup_auto, 0.20 * N)
   W = round_down_to_grain(W)
   Run [begin, begin+W) on ALL K slots via shared atomic counter
   Measure per-slot speed s_i = iters_i / busy_i
   Zero-work slots → s_i = 0.05 * median(s)

4. Plan
   R = N - W  (remaining)
   For each (a, b):
     S(a,b)    = Σ s_i for chosen slots
     T(a,b)    = R / S + fork_join(a+b)
     P(a,b)    = κ * PowerModel(a, b)
     E(a,b)    = P * T
     EDP(a,b)  = E * T
   Pick (a*, b*) = argmin EDP
   Tie-break: fewer threads, then fewer E threads
   weights w_i = s_i / S

5. Execute
   R_static = round_down_to_grain((1 - tail_fraction) * R)
   Static blocks: largest-remainder method with grain alignment
   Dynamic tail:  atomic counter, chunk = max(grain, tail_len / (8*K))

6. Energy bookkeeping
   RAPL window ≥ rapl_min_window_ms → meas_energy_j = Δuж × 1e-6

7. Online κ refinement
   P_meas = meas_energy_j / T_meas
   κ_new  = P_meas / PowerModel(a*, b*)
   κ ← 0.7κ + 0.3κ_new

8. Update LoopState: speeds, κ, imbalance, pred_err, call counter
```

### Data Flow (ASCII)

```
caller thread
    │
    ├─ detect_topology()   (singleton, first call only)
    ├─ EnergyReader        (singleton)
    ├─ PowerModel          (singleton, loads profile file)
    │
    ▼
hybrid_for_range_impl()
    │
    ├─ LoopStateRegistry::get(name)
    │
    ├─ [if need_calib]
    │      calibrate()  →  OMP parallel, all K slots
    │                      atomic counter, per-slot timing
    │
    ├─ choose_plan()    →  enumerate_candidates() → pick by EDP
    │
    ├─ compute_static_blocks()   (largest-remainder)
    │
    ├─ DynamicTail::init()
    │
    ├─ OMP parallel num_threads(K_chosen)
    │      pin_current_thread(cpu)
    │      body(static_lo, static_hi, slot)
    │      while (tail) { body(lo, hi, slot) }
    │
    ├─ RAPL Δ → meas_energy_j
    ├─ imbalance computation
    ├─ κ update
    └─ LoopState update
```

### 6.4 Power Model

```
PowerModel(a, b) = P_base
                 + min(a, nPphys) * p_phys
                 + max(0, a - nPphys) * p_smt
                 + b * p_e
```

Default values (rough; not publishable without profiling):  
`base=3.0 W`, `p_phys=4.0 W`, `p_smt=0.8 W`, `p_e=1.0 W`

Scale factor `κ` (per-loop EMA) tracks the ratio of measured to modelled power, adapting to compute-bound vs memory-bound kernels.

## Design Decisions

**Why user-level (no runtime patch)?**  
Patching libgomp requires a custom OpenMP build, is fragile across compiler versions, and cannot be deployed without replacing the system runtime. A user-level shim works with any compliant OpenMP implementation.

**Why warm-up on real iterations?**  
Re-running the same iterations (fake warm-up) doubles work and wastes time proportional to the warm-up fraction. Running a real slice produces accurate speed measurements and contributes to the final result.

**Why EDP and not time?**  
Minimising time always uses all cores. EDP captures the joint cost of energy and time: if adding E-cores saves little time but increases power significantly, EDP correctly prefers fewer cores.

**Why recalibrate per call?**  
Thermal throttling, background tasks, and memory pressure change per-core performance dynamically. A one-shot calibration (Beserra et al. style) may be stale after a few milliseconds. The overhead test quantifies when recalibration is not worth the cost.

## Limitations

1. **Conservative throughput model**: speeds measured with all K slots active are used to predict throughput of subsets. Subsets have lower contention, so subset predictions underestimate throughput. The planner may be conservative about choosing small subsets.
2. **RAPL granularity**: RAPL updates at ~1 ms intervals and covers the full package, not individual cores. Short loops get NaN energy. Package power includes memory, uncore, and IGP, not just CPU cores.
3. **Power model linearity**: the linear model ignores PL1/PL2 power limits which cause saturation at high occupancy, memory-bound power which scales differently from compute-bound, and thermal interdependence between P and E clusters.
4. **SMT modelling**: treating SMT siblings as independent slots with measured speeds is an approximation; in practice, SMT throughput depends on instruction mix and L1/L2 sharing.
5. **Short loops**: loops shorter than ~50 µs predicted time run serially; the overhead break-even point (~1–5 ms measured) means very short loops lose performance relative to a plain `#pragma omp parallel for`.
6. **Requires loop wrapping**: every parallel loop must be changed from `#pragma omp parallel for` to `hs::hybrid_for`. Automatic source transformation is not provided.
7. **Linux-only measurement**: RAPL, sysfs topology detection, and `sched_setaffinity` pinning are Linux-specific. On other platforms the library compiles and runs but uses a homogeneous fallback with no energy measurement.
