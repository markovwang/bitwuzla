# solve-before perf probes

This directory contains opt-in performance probes for Bitwuzla's
`set-solve-before` implementation. These cases are intentionally not wired into
the default Meson regression suite.

The probes are designed to separate four costs:

- priority term materialization: `decision_priority::time_bitblast`,
  `decision_priority::time_encode`, and CNF size deltas.
- registration overhead: `decision_priority::time_register`,
  `decision_priority::lits`, and native CaDiCaL priority registration counts.
- old external-propagator callback scans:
  `decision_priority::sat::cb_decide_*` should stay at zero with the native
  priority selector.
- old observed-var notification overhead:
  `decision_priority::sat::notify_*` should stay at zero with the native
  priority selector.

Build with this checkout's core cap:

```sh
meson compile -C build -j4
```

Run the default benchmark pass:

```sh
python3 test/perf/solve_before/run_solve_before_perf.py --repeat 5
```

Run a small smoke pass:

```sh
python3 test/perf/solve_before/run_solve_before_perf.py --repeat 1 --case wide_and_256
```

Collect a perf profile for one generated case:

```sh
python3 test/perf/solve_before/run_solve_before_perf.py \
  --repeat 1 \
  --case wide_and_4096 \
  --perf \
  --perf-case wide_and_4096_sb
```

The runner writes generated SMT2 files, detailed JSON, CSV summaries, and perf
reports under `/tmp/bitwuzla-sb-perf` by default.

Run an opt-in seed sweep for model-balance checks:

```sh
python3 test/perf/solve_before/run_solve_before_seed_sweep.py --seeds 64
```

The seed sweep checks that the same seed is reproducible and reports the
cross-seed bit-one fractions for a small symmetric `set-solve-before` case.
