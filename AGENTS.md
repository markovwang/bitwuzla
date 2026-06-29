# Agent Handoff: Bitwuzla Solve-Before

This file is for Codex or other coding agents continuing work in this checkout.
It is not upstream Bitwuzla policy. Verify the current branch and diff before
editing, because this workspace may contain local untracked notes or generated
files.

## Scope

Repository: `/export/markovw/solve_before_bzla/bitwuzla`

Main feature area: `solve-before` decision priority for Bitwuzla.

Known branch context: `solve-before-v0`, with the native CaDiCaL priority
direction landed in commit `7e070dd6 Use native CaDiCaL priority for
solve-before`.

## Operating Rules

- Keep edits tightly scoped to `solve-before` unless the user explicitly asks
  for broader cleanup.
- Do not stage or rewrite unrelated local files such as `compile_commands.json`,
  draft spec translations, editor swap files, or generated perf output.
- If compiling or rebuilding, use 4 cores or fewer. Prefer:

```sh
meson compile -C build -j4
```

- Do not use higher parallelism for this checkout.
- Perf probes under `test/perf/solve_before/` are opt-in diagnostics, not
  default regression tests.
- Network access may be restricted. If comparing to upstream, clearly state
  whether the baseline is the local `origin/*` reference or a freshly fetched
  remote.

## Current Design Direction

The durable direction is native CaDiCaL decision priority, not the older
external-propagator prototype.

The important design points are:

- `set-solve-before` is a decision-priority hint. It does not add constraints
  and does not promise SystemVerilog-style randomization semantics.
- Bitwuzla computes solve-before tiers, bit-blasts the relevant terms, and
  registers their SAT literals with the SAT backend.
- The CaDiCaL backend calls native priority APIs:
  `set_decision_priority_seed`, `clear_decision_priority`,
  `add_decision_priority_lit`, and `decision_priority_stats`.
- Priority variables remain normal CaDiCaL internal variables. The priority
  set is only an auxiliary index over existing variables, not a separate
  variable pool.
- Priority variables must be frozen while registered, then melted on
  `clear_decision_priority()`. Without this, preprocessing can eliminate them
  before the CDCL decision path sees the priority.
- On assignment, a priority variable is removed from the candidate set. On
  backtrack/unassign, it is re-enqueued if it is still registered and eligible.
- When active native priority buckets exist, CaDiCaL must skip `lucky_phases()`;
  otherwise lucky SAT can produce a model before the native priority selector
  runs.
- Old external-propagator metrics such as `cb_decide_*`, `notify_*`, and
  observed-var counts should stay at zero for the native path.

## Key Files

Bitwuzla integration:

- `src/parser/smt2/parser.cpp`
- `src/solving_context.cpp`
- `src/solver/solver_engine.cpp`
- `src/solver/bv/bv_solver.cpp`
- `src/solver/bv/bv_bitblast_solver.cpp`
- `src/solver/bv/bv_bitblast_solver.h`
- `src/sat/sat_solver.h`
- `src/sat/cadical.cpp`
- `src/sat/cadical.h`
- `src/sat/sat_solver_factory.cpp`

Vendored CaDiCaL priority support:

- `subprojects/packagefiles/cadical/src/cadical.hpp`
- `subprojects/packagefiles/cadical/src/solver.cpp`
- `subprojects/packagefiles/cadical/src/internal.hpp`
- `subprojects/packagefiles/cadical/src/decide.cpp`
- `subprojects/packagefiles/cadical/src/lucky.cpp`
- `subprojects/packagefiles/cadical/src/backtrack.cpp`
- `subprojects/packagefiles/cadical/src/propagate.cpp`
- `subprojects/packagefiles/cadical/src/flags.cpp`

If editing vendored CaDiCaL, make sure the source used by the build and the
Meson package overlay stay consistent.

Tests and probes:

- `test/unit/sat/test_cadical_decision_priority.cpp`
- `test/regress/parser/solve_before*.smt2`
- `test/perf/solve_before/README.md`
- `test/perf/solve_before/run_solve_before_perf.py`
- `test/perf/solve_before/run_solve_before_seed_sweep.py`
- `test/perf/solve_before/seed_cases/`

Readable handoff:

- `SOLVE_BEFORE_NATIVE_PRIORITY_PLAN_ZH.md`

## Suggested Verification

Use the minimum verification needed for the change. For core solve-before or
CaDiCaL priority changes, the usual targeted set is:

```sh
meson compile -C build -j4
meson test -C build sat_cadical_decision_priority --print-errorlogs
meson test -C build parser_solve_before.smt2 parser_solve_before_chain.smt2 parser_solve_before_cycle.smt2 parser_solve_before_self.smt2 --print-errorlogs
python3 test/perf/solve_before/run_solve_before_seed_sweep.py --binary build/src/main/bitwuzla --seeds 32 --width 16
```

For performance questions, separate these costs instead of only reporting total
runtime:

- priority term materialization: bitblast and encode time, CNF size deltas;
- registration overhead: priority terms, bits, literals, duplicates;
- SAT search effects: native priority decisions, fallbacks, phase counts;
- old external path leakage: `cb_decide_*`, `notify_*`, observed-var counts.

## Common Pitfalls

- Do not reintroduce external propagator as the main solve-before backend unless
  the user explicitly asks for that experiment.
- Do not treat seed variation as strict uniform model sampling. The intended
  check is same-seed reproducibility plus cross-seed balance.
- Do not use weak seed probes like `A & B = 0`; all-zero models can hide phase
  behavior. Prefer XOR-style cases that force per-bit choices.
- Do not assume priority variables can be tracked outside CaDiCaL's normal
  variable pool. They must remain normal variables so assignment, propagation,
  conflict analysis, and backtrack all work through existing CaDiCaL machinery.
