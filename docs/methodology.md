# Methodology

## Measurement Protocol

### Hardware and OS Configuration

Before running any benchmark:

1. **Power**: machine must be plugged in to mains power. Battery power changes TDP budgets and throttling behaviour.
2. **Governor**: set to `performance` using `scripts/stable_mode.sh`. The `powersave` governor causes frequency scaling that adds noise.
3. **Turbo**: optional — optionally disable with `--no-turbo` for reproducibility; document which mode was used.
4. **Thermal state**: allow 10–15 minutes of idle time after heavy work to let temperatures stabilise. Record `temp_c_before` and `temp_c_after` to detect thermal throttling.
5. **Background processes**: close browsers, editors, and other CPU-intensive tasks. Document with `ps aux` if needed.
6. **Environment**: same room temperature across runs within one experiment.

Do **not** mix runs from different power modes in the same analysis.

### Repetitions and Statistics

- Minimum **10 measured repetitions** per (kernel, variant) configuration for the compare suite, more for noisy measurements.
- **2 warm-up repetitions** before measured ones (not recorded). Warm-up allows the CPU to reach steady-state frequency and fills caches.
- **Cooldown of 1000 ms** between runs in the compare suite to allow thermal settling and reduce carry-over effects.
- Variant order is **randomised within each repetition block** (Fisher-Yates shuffle, deterministic seed) to prevent systematic thermal-drift bias. The compare suite implements this directly.
- Report **mean ± 95% CI** (Student's t-distribution, df = n−1) for time, energy, and EDP.
- Report **speedup and EDP ratios** with 95% CI via **bootstrap** (10 000 resamples, fixed seed 42). Flag ratios whose CI straddles 1.0 as not statistically significant.

### Overhead Measurement

The overhead suite runs `uniform_fma` (identical work per iteration) under:
- `omp_static` pinned to the same CPU subset as the hybrid executor.
- `hybrid_edp` with `EVERY_CALL` recalibration.

Overhead = `(hybrid_time − omp_time) / omp_time`.

The **break-even loop duration** is the shortest loop where hybrid overhead is < 5%.

### Oracle Construction

For each kernel and size class, the oracle suite runs every `(a, b)` combination (all P/E subsets) using an OMP mixed-affinity executor. The best-EDP config found exhaustively is the oracle. The hybrid scheduler's EDP regret = `hybrid_EDP / oracle_EDP`.

## RAPL Caveats

- **Package scope only**: RAPL package-0 covers all CPU cores, the integrated GPU, and uncore logic. It does not isolate CPU-only energy, and DRAM is a separate domain not always accessible on client chips.
- **Coarse and noisy at short windows**: the RAPL counter updates at approximately 1 ms. Measurements shorter than 20 ms (configurable via `HS_RAPL_MIN_MS`) are marked NaN and excluded from analysis.
- **No per-core granularity**: the package counter cannot distinguish P-core from E-core energy. The power model provides this estimation.
- **Access restricted since Linux 5.10**: the PLATYPUS side-channel mitigation requires root or a `chmod a+r` on `energy_uj` files. The `scripts/rapl_permissions.sh` script handles this; it resets on reboot.
- **Counter wrap**: the 64-bit counter wraps after `max_energy_range_uj` microjoules (typically ~262 J for a 15 W TDP chip running for ~17 s at full power). The library handles single wraps; double-wraps in a single call are not handled but require impossibly long loops.
- **Trustworthy results require**: window ≥ 20 ms, stable power mode, no concurrent GPU workload, and proper kernel support.

## CSV Schema

See `results/sample/README.md` for the full column definitions.

## Threats to Validity

1. **Thermal throttling**: if the chip hits its TDP budget, P-core frequency drops; the hybrid method's advantage may appear or disappear depending on thermal state. Always record `temp_c_before/after` and `p_khz_after`.
2. **RAPL accuracy**: package-level energy includes memory, IGP, and uncore; net-energy (subtracting idle power) partially corrects this but idle power also varies.
3. **Warm-up duration**: the 3% warm-up fraction is a heuristic. For very short loops (< 1 ms) the warm-up may dominate; these are reported as overhead and excluded from the main comparison.
4. **Single-socket, single-machine**: results generalise only to similar Intel hybrid chips. The conclusions may differ on AMD, ARM big.LITTLE, or server CPUs.
5. **Workload representativeness**: the eight kernels cover memory-bound (STREAM), compute-bound (matmul, MLP), irregular (BFS, Mandelbrot), and iterative (K-means) patterns. Other workload types (e.g. I/O-bound, GPU-offloaded) are not covered.
