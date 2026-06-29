# Agent 交接说明：Bitwuzla Solve-Before

这个文件给后续 Codex 或其他 coding agent 使用，用来继续当前 checkout 里的
`solve-before` 工作。它不是 Bitwuzla upstream 的通用规则。动手前先确认当前
分支和 diff，因为这个 workspace 里可能有本地未跟踪的笔记或生成文件。

## 范围

仓库：`/export/markovw/solve_before_bzla/bitwuzla`

主要功能：Bitwuzla 的 `solve-before` decision priority。

已知分支上下文：`solve-before-v0`，native CaDiCaL priority 方向已经落在
commit `7e070dd6 Use native CaDiCaL priority for solve-before`。

## 操作规则

- 除非用户明确要求，改动范围限制在 `solve-before` 相关文件。
- 不要 stage 或重写无关本地文件，例如 `compile_commands.json`、spec 翻译草稿、
  editor swap 文件、生成的 perf 输出。
- 如果需要编译或重建，使用 4 core 或更少。优先使用：

```sh
meson compile -C build -j4
```

- 当前 checkout 不要使用更高并行度。
- `test/perf/solve_before/` 下的 perf probes 是 opt-in 诊断，不是默认回归。
- 网络可能受限。对比 upstream 时，要明确 baseline 是本地 `origin/*` 引用，
  还是刚 fetch 的远端。

## 当前设计方向

稳定方向是 native CaDiCaL decision priority，不是旧的 external propagator 原型。

关键点：

- `set-solve-before` 是 decision-priority hint，不新增约束，也不承诺
  SystemVerilog randomization 那种严格分布语义。
- Bitwuzla 计算 solve-before tiers，把相关 term bit-blast 成 SAT literal，
  再注册到 SAT backend。
- CaDiCaL backend 调用 native priority API：
  `set_decision_priority_seed`、`clear_decision_priority`、
  `add_decision_priority_lit`、`decision_priority_stats`。
- priority 变量仍然是 CaDiCaL 原来的 internal variable。priority set 只是
  覆盖在现有变量上的辅助索引，不是第二套变量池。
- priority 变量注册期间必须 freeze，`clear_decision_priority()` 时再 melt。
  否则 preprocessing 可能在 CDCL decision path 看到 priority 前就消掉它们。
- priority 变量被 assign 时，从 candidate set 中移除；backtrack/unassign 时，
  如果仍然 registered 且 eligible，就重新 enqueue。
- 存在 active native priority bucket 时，CaDiCaL 必须跳过 `lucky_phases()`；
  否则 lucky SAT 可能在 native priority selector 运行前就直接产出模型。
- native path 下旧 external-propagator 指标应保持为零，包括 `cb_decide_*`、
  `notify_*` 和 observed-var 计数。

## 关键文件

Bitwuzla 集成：

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

Vendored CaDiCaL priority 支持：

- `subprojects/packagefiles/cadical/src/cadical.hpp`
- `subprojects/packagefiles/cadical/src/solver.cpp`
- `subprojects/packagefiles/cadical/src/internal.hpp`
- `subprojects/packagefiles/cadical/src/decide.cpp`
- `subprojects/packagefiles/cadical/src/lucky.cpp`
- `subprojects/packagefiles/cadical/src/backtrack.cpp`
- `subprojects/packagefiles/cadical/src/propagate.cpp`
- `subprojects/packagefiles/cadical/src/flags.cpp`

如果修改 vendored CaDiCaL，确认 build 实际使用的源码和 Meson package overlay 保持一致。

测试和 probe：

- `test/unit/sat/test_cadical_decision_priority.cpp`
- `test/regress/parser/solve_before*.smt2`
- `test/perf/solve_before/README.md`
- `test/perf/solve_before/run_solve_before_perf.py`
- `test/perf/solve_before/run_solve_before_seed_sweep.py`
- `test/perf/solve_before/seed_cases/`

可读方案说明：

- `SOLVE_BEFORE_NATIVE_PRIORITY_PLAN_ZH.md`

## 建议验证

按改动范围选择最小验证。修改 solve-before 核心或 CaDiCaL priority 时，通常跑：

```sh
meson compile -C build -j4
meson test -C build sat_cadical_decision_priority --print-errorlogs
meson test -C build parser_solve_before.smt2 parser_solve_before_chain.smt2 parser_solve_before_cycle.smt2 parser_solve_before_self.smt2 --print-errorlogs
python3 test/perf/solve_before/run_solve_before_seed_sweep.py --binary build/src/main/bitwuzla --seeds 32 --width 16
```

性能问题不要只看总时间，要分开看：

- priority term materialization：bitblast / encode 时间，CNF size delta；
- registration overhead：priority terms、bits、lits、duplicates；
- SAT search effect：native priority decisions、fallbacks、phase 计数；
- 旧 external path 泄漏：`cb_decide_*`、`notify_*`、observed-var 计数。

## 常见坑

- 除非用户明确要求实验，不要把 external propagator 重新作为 solve-before 主后端。
- 不要把 seed variation 当成严格均匀模型采样。这里检查的是同 seed 可复现、
  跨 seed 大致均衡。
- 不要用 `A & B = 0` 这类弱 seed probe；全零模型会掩盖 phase 行为。优先用
  XOR 类用例强制每个 bit 二选一。
- 不要假设 priority 变量可以脱离 CaDiCaL 原变量池单独跟踪。它们必须仍然是
  普通 CaDiCaL 变量，这样 assignment、propagation、conflict analysis、
  backtrack 才能全部走现有机制。
