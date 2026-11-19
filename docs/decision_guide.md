# Decision Guide

> **This file is a template only.**
> Run `hs_decision_guide results/raw/<timestamp>/sweep.csv --out docs/decision_guide.md`
> to auto-generate this document from real sweep data.

## Per-Kernel Best Configurations

| Kernel | Best-Time Variant | Best-EDP Variant | Note |
|--------|------------------|------------------|------|
| stream_triad | *(run hs_decision_guide)* | *(run hs_decision_guide)* | |
| mandelbrot | | | |
| matmul | | | |
| mlp_train | | | |
| conv_layer | | | |
| kmeans | | | |
| bfs | | | |
| uniform_fma | | | |

## How to Read This Table

- **Best-Time**: the variant (schedule, chunk size, thread count, affinity, core subset) with the lowest mean wall-clock time across all measured reps.
- **Best-EDP**: the variant with the lowest mean Energy-Delay Product (`E × T`).

When the two columns agree, a single configuration is optimal for both time and energy-efficiency. When they differ, there is a tradeoff: choose based on whether you are latency-sensitive or energy-sensitive.

## Caveats

- These results are specific to the machine, governor, power mode, and thermal state during the sweep.
- EDP values require RAPL access; rows with `NaN` energy are excluded from EDP ranking.
- Re-run the sweep after any change to the system configuration (kernel update, BIOS settings, power limit change).
