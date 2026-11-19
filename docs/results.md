# Results

> **This file is a template only.**
> Run the experiment pipeline (`experiments/run_all.sh`) and then
> `hs_analyze results/raw/<timestamp>/compare.csv --out docs/results.md`
> to populate the tables below with real measurements.

## System Configuration

| Field | Value |
|-------|-------|
| CPU model | *(fill after run)* |
| P-cores (physical) | *(fill)* |
| P-cores (logical, with SMT) | *(fill)* |
| E-cores | *(fill)* |
| OS | *(fill)* |
| Kernel | *(fill)* |
| Governor | *(fill)* |
| Turbo | *(fill: enabled / disabled)* |
| RAPL available | *(fill)* |
| Power profile source | *(fill: measured / defaults)* |
| Timestamp | *(fill)* |

## Kernel: stream_triad

| Variant | Time (s) ± CI | Energy (J) ± CI | EDP ± CI | Speedup vs omp_static | EDP ratio vs omp_static | Imbalance % |
|---------|--------------|----------------|---------|----------------------|------------------------|-------------|
| omp_static | | | | 1.00 (baseline) | 1.00 (baseline) | |
| omp_dynamic | | | | | | |
| omp_guided | | | | | | |
| hybrid_time_balanced | | | | | | |
| hybrid_edp | | | | | | |
| hybrid_energy | | | | | | |
| fixed_ratio | | | | | | |

## Kernel: mandelbrot

| Variant | Time (s) ± CI | Energy (J) ± CI | EDP ± CI | Speedup vs omp_static | EDP ratio vs omp_static | Imbalance % |
|---------|--------------|----------------|---------|----------------------|------------------------|-------------|
| omp_static | | | | 1.00 | 1.00 | |
| omp_dynamic | | | | | | |
| hybrid_time_balanced | | | | | | |
| hybrid_edp | | | | | | |

## Kernel: matmul

| Variant | Time (s) ± CI | Energy (J) ± CI | EDP ± CI | Speedup vs omp_static | EDP ratio vs omp_static | Imbalance % |
|---------|--------------|----------------|---------|----------------------|------------------------|-------------|
| omp_static | | | | 1.00 | 1.00 | |
| hybrid_time_balanced | | | | | | |
| hybrid_edp | | | | | | |

## Kernel: mlp_train

| Variant | Time (s) ± CI | Energy (J) ± CI | EDP ± CI | Speedup vs omp_static | EDP ratio vs omp_static | Imbalance % |
|---------|--------------|----------------|---------|----------------------|------------------------|-------------|
| omp_static | | | | 1.00 | 1.00 | |
| hybrid_time_balanced | | | | | | |
| hybrid_edp | | | | | | |

## Kernel: conv_layer

| Variant | Time (s) ± CI | Energy (J) ± CI | EDP ± CI | Speedup vs omp_static | EDP ratio vs omp_static | Imbalance % |
|---------|--------------|----------------|---------|----------------------|------------------------|-------------|
| omp_static | | | | 1.00 | 1.00 | |
| hybrid_time_balanced | | | | | | |
| hybrid_edp | | | | | | |

## Kernel: kmeans

| Variant | Time (s) ± CI | Energy (J) ± CI | EDP ± CI | Speedup vs omp_static | EDP ratio vs omp_static | Imbalance % |
|---------|--------------|----------------|---------|----------------------|------------------------|-------------|
| omp_static | | | | 1.00 | 1.00 | |
| hybrid_time_balanced | | | | | | |
| hybrid_edp | | | | | | |

## Kernel: bfs

| Variant | Time (s) ± CI | Energy (J) ± CI | EDP ± CI | Speedup vs omp_static | EDP ratio vs omp_static | Imbalance % |
|---------|--------------|----------------|---------|----------------------|------------------------|-------------|
| omp_static | | | | 1.00 | 1.00 | |
| hybrid_time_balanced | | | | | | |
| hybrid_edp | | | | | | |

## Framework Overhead

| Loop duration | omp_static time (ms) | hybrid_edp time (ms) | Overhead % |
|--------------|---------------------|---------------------|-----------|
| ~10 µs | | | |
| ~50 µs | | | |
| ~100 µs | | | |
| ~1 ms | | | |
| ~5 ms | | | |
| ~50 ms | | | |
| ~500 ms | | | |

Break-even loop duration (overhead < 5%): *(fill)*

## Oracle Regret

| Kernel | Our EDP | Oracle EDP | Regret (our/oracle) |
|--------|---------|-----------|---------------------|
| stream_triad | | | |
| mandelbrot | | | |
| matmul | | | |
| mlp_train | | | |
| conv_layer | | | |
| kmeans | | | |
| bfs | | | |

## Wins / Ties / Losses vs TIME_BALANCED

Per kernel, whether `hybrid_edp` EDP CI excludes 1.0 relative to `hybrid_time_balanced`.

| Kernel | Verdict | EDP ratio (95% CI) |
|--------|---------|-------------------|
| stream_triad | *(fill)* | |
| mandelbrot | *(fill)* | |
| matmul | *(fill)* | |
| mlp_train | *(fill)* | |
| conv_layer | *(fill)* | |
| kmeans | *(fill)* | |
| bfs | *(fill)* | |
