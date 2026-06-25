# solve-before seed result probes

These SMT2 files are opt-in probes for checking whether different `--seed`
values can lead to different satisfying models while preserving reproducibility
for the same seed.

Example:

```sh
build/src/main/bitwuzla --seed 1 test/perf/solve_before/seed_cases/xor_bv16_sb.smt2
build/src/main/bitwuzla --seed 2 test/perf/solve_before/seed_cases/xor_bv16_sb.smt2
```

There are also self-contained variants using SMT-LIB `:random-seed`:

```sh
build/src/main/bitwuzla test/perf/solve_before/seed_cases/xor_bv16_sb_seed1.smt2
build/src/main/bitwuzla test/perf/solve_before/seed_cases/xor_bv16_sb_seed2.smt2
```

Each file enables model production and prints selected values with `get-value`.
They are intentionally not part of the default Meson regression suite.
