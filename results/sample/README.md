# CSV Schema

Each row in a results CSV corresponds to one measured repetition of one (kernel, variant) pair.

## Columns

| Column | Type | Description |
|--------|------|-------------|
| `timestamp_ms` | int | Unix timestamp in milliseconds when the run started |
| `host_cpu` | str | CPU model string from `/proc/cpuinfo` |
| `kernel` | str | Kernel name (e.g. `stream_triad`, `mlp_train`) |
| `size_class` | str | `tiny`, `small`, `medium`, or `large` |
| `variant` | str | Executor label (e.g. `omp_static,c=0,t=12,aff=ORDERED`) |
| `rep` | int | 0-based repetition index within the run |
| `time_s` | float | Wall-clock elapsed time in seconds (steady_clock) |
| `energy_j_gross` | float | Gross package energy in joules (RAPL); `NaN` if unavailable |
| `energy_j_net` | float | Net energy = gross − idle_power × time; `NaN` if idle power not measured |
| `avg_power_w` | float | Average package power = gross energy / time; `NaN` if energy unavailable |
| `edp` | float | Energy-Delay Product = energy_j_gross × time_s; `NaN` if energy unavailable |
| `imbalance_pct` | float | Thread imbalance = (T_max − T_mean) / T_max × 100 |
| `calib_overhead_s` | float | Time spent in warm-up + planning (hybrid variants only) |
| `a_p_threads` | int | Number of P-core slots used in main phase |
| `b_e_threads` | int | Number of E-core slots used in main phase |
| `n_recalib` | int | Number of calibration events in this run |
| `pred_err_pct` | float | `|T_pred − T_meas| / T_meas × 100` (hybrid only); `NaN` otherwise |
| `temp_c_before` | float | Package temperature before the run in °C; `-1` if unavailable |
| `temp_c_after` | float | Package temperature after the run in °C |
| `p_khz_after` | int | Scaling frequency of first P-core after run (kHz) |
| `e_khz_after` | int | Scaling frequency of first E-core after run (kHz); `0` if no E-cores |
| `governor` | str | CPU frequency governor at run time |
| `verified` | int | `1` if `kernel.verify()` passed, `0` otherwise, empty if not checked |
| `work_units` | float | Domain-specific work (FLOPs, bytes, edges) |
| `work_unit_name` | str | `FLOP`, `byte`, `edges`, etc. |
| `throughput` | float | `work_units / time_s` |

## Example (hand-written, not real measurements)

```csv
timestamp_ms,host_cpu,kernel,size_class,variant,rep,time_s,energy_j_gross,energy_j_net,avg_power_w,edp,imbalance_pct,calib_overhead_s,a_p_threads,b_e_threads,n_recalib,pred_err_pct,temp_c_before,temp_c_after,p_khz_after,e_khz_after,governor,verified,work_units,work_unit_name,throughput
1700000000000,Intel Core i5-1235U,stream_triad,medium,omp_static;c=0;t=12;aff=ORDERED,0,0.1234,1.8500,1.5200,15.0,0.2283,12.3,0.0000,0,0,0,NaN,52.0,58.0,2400000,1800000,performance,1,25165824.0,FLOP,203894854.0
1700000000135,Intel Core i5-1235U,stream_triad,medium,hybrid_edp;calib=every,0,0.1189,1.7800,1.4600,14.97,0.2117,8.1,0.0023,2,4,1,5.2,52.0,57.0,2400000,1800000,performance,1,25165824.0,FLOP,211655341.0
```

> **Note**: The example values above are illustrative only and were not produced by running the code.
> Real measurements depend on hardware, thermal state, and workload characteristics.
