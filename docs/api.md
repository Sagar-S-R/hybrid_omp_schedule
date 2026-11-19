# API Reference

## Core Entry Points

### `hs::hybrid_for`

```cpp
template <class Body>
void hybrid_for(const char* loop_name,
                std::size_t begin, std::size_t end,
                Body&& body,
                const Options& opts = {});
```

Drop-in replacement for `#pragma omp parallel for` with per-iteration body.

**Parameters**
- `loop_name` — stable string identifier (e.g. `"myapp.gemm_rows"`). Used as key for per-loop persistent state (calibration cache, kappa). Pass `nullptr` to share anonymous state; a one-time warning is emitted.
- `begin`, `end` — half-open iteration range `[begin, end)`.
- `body` — callable accepting `std::size_t i`. Called exactly once per index.
- `opts` — see `Options` below.

**Guarantees**
- Every index in `[begin, end)` is executed exactly once.
- Exceptions thrown by `body` are captured, parallel work is drained, and the first exception is rethrown in the caller's thread.
- If called from inside an OpenMP parallel region (`omp_in_parallel()`), runs serially.

---

### `hs::hybrid_for_range`

```cpp
template <class RangeBody>
void hybrid_for_range(const char* loop_name,
                      std::size_t begin, std::size_t end,
                      RangeBody&& body,
                      const Options& opts = {});
```

Range-body version: `body(lo, hi, slot)` processes `[lo, hi)`. `slot` is the 0-based worker slot index, stable within a call.

---

### `hs::hybrid_reduce`

```cpp
template <class T, class Body, class Combine>
T hybrid_reduce(const char* loop_name,
                std::size_t begin, std::size_t end,
                T identity,
                Body&& body,           // (std::size_t i, T& acc)
                Combine&& combine,     // (T, T) -> T
                const Options& opts = {});
```

Parallel reduction. Each slot gets a private accumulator initialised to `identity`. `body(i, acc)` accumulates into `acc`. Slots are combined in slot order (deterministic for floating-point if combine is associative).

---

## `Options` Struct

```cpp
struct Options {
    Policy      policy               = Policy::EDP;
    Recalibrate recalibrate          = Recalibrate::EVERY_CALL;
    int         recalibrate_every_n  = 8;
    double      drift_threshold      = 0.15;
    double      warmup_fraction      = 0.03;
    std::size_t min_warmup_iters     = 0;       // 0 = auto
    double      tail_fraction        = 0.10;
    std::size_t grain                = 1;
    bool        use_smt              = true;
    double      fixed_ratio_p_over_e = 2.0;
    double      min_parallel_seconds = 50e-6;
    LoopStats*  stats_out            = nullptr;
};
```

| Field | Meaning |
|-------|---------|
| `policy` | Core-selection objective (see `Policy` enum) |
| `recalibrate` | When to re-measure per-slot speeds |
| `recalibrate_every_n` | Interval for `EVERY_N` mode |
| `drift_threshold` | Imbalance or pred-error fraction that triggers `ON_DRIFT` recalibration |
| `warmup_fraction` | Fraction of N used for warm-up (default 3%) |
| `min_warmup_iters` | Hard minimum; 0 = `8 * n_slots` |
| `tail_fraction` | Fraction of remaining iterations left for dynamic dispatch |
| `grain` | All block sizes are multiples of this |
| `use_smt` | If false, only primary P threads are used |
| `fixed_ratio_p_over_e` | Weight ratio for `FIXED_RATIO` policy |
| `min_parallel_seconds` | Predicted time below which loop runs serially |
| `stats_out` | Pointer to `LoopStats` filled after each call |

---

## `Policy` Enum

| Value | Description |
|-------|-------------|
| `EDP` | **(default / our method)** Pick (a, b) minimising predicted `E × T` |
| `ENERGY` | Pick (a, b) minimising predicted energy only |
| `TIME_BALANCED` | Always use all slots; weights by measured speed (Beserra-style) |
| `P_ONLY` | Force P-slots only |
| `E_ONLY` | Force E-slots only |
| `FIXED_RATIO` | No calibration; fixed P:E weight ratio |

---

## `Recalibrate` Enum

| Value | Description |
|-------|-------------|
| `EVERY_CALL` | **(default)** Measure speeds on every call |
| `EVERY_N` | Every `recalibrate_every_n`-th call |
| `ON_DRIFT` | When imbalance or prediction error exceeds `drift_threshold` |
| `ONCE` | Only on the first call for this loop name |

---

## `LoopStats` Struct

Filled when `Options::stats_out` is non-null.

| Field | Description |
|-------|-------------|
| `n_iters` | Total iterations |
| `slots_used`, `p_threads`, `e_threads` | Core assignment used |
| `warmup_seconds`, `plan_seconds`, `main_seconds`, `total_seconds` | Phase timings |
| `pred_time_s`, `pred_power_w`, `pred_energy_j`, `pred_edp` | Predicted values |
| `meas_energy_j` | Measured package energy (NaN if RAPL unavailable) |
| `imbalance_percent` | `(T_max − T_mean) / T_max × 100` |
| `slot_speed_iters_per_s` | Per-slot speeds from last calibration |
| `slot_finish_s` | Per-slot finish times relative to main-phase start |
| `calibrated_this_call` | Whether calibration was run this call |

---

## Runtime Control

```cpp
RuntimeInfo runtime_info();         // topology summary, RAPL, power profile status
void set_default_options(const Options&); // override global defaults
void reset_loop_state();            // forget all per-loop state (use between tests)
void shutdown();                    // optional cleanup
```

---

## Environment Variables

| Variable | Effect |
|----------|--------|
| `HS_VERBOSE=1` | Enable diagnostic logging to stderr |
| `HS_TRACE_CSV=path` | Append one row per call to this CSV |
| `HS_P_CPUS=cpulist` | Override P-core CPU list (e.g. `0-3`) |
| `HS_E_CPUS=cpulist` | Override E-core CPU list (e.g. `4-11`) |
| `HS_POWER_PROFILE=path` | Load power profile from this file |
| `HS_FORCE_HOMOGENEOUS=1` | Treat all CPUs as P-cores (used in CI) |
| `HS_DISABLE_PINNING=1` | Disable `sched_setaffinity` |
| `HS_RAPL_MIN_MS=N` | Minimum RAPL window in ms (default 20) |
