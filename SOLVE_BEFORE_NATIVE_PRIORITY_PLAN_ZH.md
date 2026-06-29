# Solve-Before Native Priority 最终方案

日期：2026-06-26

来源 session：`019eee23-670f-79b2-b30c-4ffd3e50bf17`

当前分支：`solve-before-v0`

当前提交：`7e070dd6 Use native CaDiCaL priority for solve-before`

## 结论

最终方案是：不要再用 CaDiCaL external propagator 来模拟 `solve-before` 的决策顺序，而是在 vendored CaDiCaL 1.7.4 内部加入原生 decision-priority selector。

`solve-before` 仍然是一个 SAT decision hint，不改变 SMT-LIB / C++ API 的语义边界，不新增约束，也不承诺 SystemVerilog randomize 那种严格分布语义。它只影响 SAT 求解时优先决策哪些变量，以及在同一 priority tier 内如何用 seed 派生顺序和 phase。

## 为什么放弃 external propagator

原型方案把 priority literal 注册成 external propagator observed vars，然后通过 `cb_decide()` 返回优先 literal。这个方向能快速证明概念，但性能和语义上都有问题。

主要问题：

- CaDiCaL 一旦连接 external propagator，就进入 observed-var / callback / notify 路径，哪怕 `solve-before` 实际只需要 decision hint。
- `cb_decide()` 需要扫描 priority buckets，宽 BV 或大量 priority bits 下扫描成本会放大。
- observed vars 的 freeze / notification 成本不是 `solve-before` 真正需要的功能。
- 原实现按注册顺序扫描，并返回带符号 literal，容易引入固定顺序和固定 phase 偏置。
- 它还可能让 `lucky_phases()` 等 CaDiCaL 内部路径绕开我们想验证的 decision-priority 逻辑。

所以最终方向是：把 priority 直接接进 CaDiCaL 的 native decision selection，而不是借 external propagator 绕路。

## 目标

这个方案的目标有四个：

1. 保留 `solve-before` 作为 decision-priority hint 的语义。
2. 去掉 external propagator 带来的 callback、notify、observed-var 扫描成本。
3. 保证同一 seed 可复现，不同 seed 下同层变量顺序和 phase 不被固定注册顺序锁死。
4. 让性能验证能区分三类成本：priority term materialization、priority registration、SAT search / decision path。

“均衡性”的标准是跨 seed 均衡，不是严格均匀采样 SAT 解空间。

## 实现结构

Bitwuzla 侧继续负责从 `set-solve-before` 计算 priority tiers，并在 BV bitblast 后把对应 SAT literal 注册给 SAT backend。

关键路径：

- SMT2 parser 解析 `(set-solve-before A B)`。
- solving context 计算 solve-before graph 的拓扑 tier。
- BV solver 把 tier term bit-blast / encode 成 SAT literal。
- SAT wrapper 调用 native CaDiCaL priority API。
- CaDiCaL 在自己的 decision selector 内优先选择 priority vars。

相关入口：

- `src/solver/solver_engine.cpp`
- `src/solver/bv/bv_solver.cpp`
- `src/solver/bv/bv_bitblast_solver.cpp`
- `src/sat/cadical.cpp`
- `src/sat/sat_solver_factory.cpp`

CaDiCaL 侧新增 native API：

- `set_decision_priority_seed(seed)`
- `clear_decision_priority()`
- `add_decision_priority_lit(lit, priority)`
- `decision_priority_stats()`

对应源码在：

- `subprojects/packagefiles/cadical/src/cadical.hpp`
- `subprojects/packagefiles/cadical/src/solver.cpp`
- `subprojects/packagefiles/cadical/src/internal.hpp`
- `subprojects/packagefiles/cadical/src/decide.cpp`

## CaDiCaL 内部策略

CaDiCaL 内部维护 priority candidate set。

每个 priority var 记录：

- priority tier，数值越小越早选；
- seed-derived tie key，用于同一 tier 内排序；
- seed-derived phase，用于避免固定 polarity；
- generation，用于处理候选集合中的旧 entry；
- registered / enqueued 等状态。

决策时：

1. 先从 priority candidate set 取当前最优候选。
2. 丢弃已赋值、inactive、eliminated 或过期候选。
3. 如果找到有效 priority var，就用 seed-derived phase 返回 decision literal。
4. 如果没有有效 priority var，再 fallback 到 CaDiCaL 原有 `next_decision_variable()` 路径。

这样避免了 external propagator 的 `cb_decide()` 全量扫描，也避免了 observed-var callback 路径。

## Freeze / Melt 处理

一个关键修正是：native priority 不能只注册候选，还必须保证 priority vars 不被 preprocessing / elimination 直接消掉。

原 external-propagator 方案通过 observed vars 间接 freeze 了相关变量。切到 native priority 后，如果不补 freeze，CaDiCaL 可能在 CDCL decision 前就把这些变量消掉，最终模型由 reconstruction / completion 决定，seed-derived phase 看不到效果。

最终方案是在 native priority 内部处理：

- 首次注册 priority var 时做 internal freeze。
- `clear_decision_priority()` 时 melt 掉这层 priority 引用。

这样保留 priority var 的可决策性，但不连接 external propagator，也不产生 observed-var notify 成本。

## Lucky Phase 修正

排查 seed 不生效时发现，Bitwuzla 已经把 seed 传到了 CaDiCaL，priority literals 也已经注册，但 `native_decide_calls=0`。也就是说问题不是 seed plumbing，而是求解在进入 CDCL decision path 前已经结束。

直接原因是 CaDiCaL 的 `lucky_phases()` 会在 CDCL decision 前尝试固定模式赋值，例如全 false / 全 true / 顺序 phase。对于某些宽 BV XOR 类用例，它能直接找到 SAT model，导致 native decision-priority selector 完全没有机会运行。

因此最终补丁是：当存在 active native decision priority bucket 时，跳过 `lucky_phases()`，让 `solve-before` 的 priority 和 seed-derived phase/order 真正进入 CDCL decision path。

相关入口：

- `subprojects/packagefiles/cadical/src/lucky.cpp`
- `subprojects/packagefiles/cadical/src/internal.cpp`

这个修正不是单个 SMT2 用例 workaround，而是 native decision-priority extension 的配套规则：如果 priority hint 存在，不能让 lucky SAT 路径绕过 priority selector。

## 统计与验证资产

保留了面向诊断的统计，主要用于区分问题来源：

- priority term materialization：bitblast / encode 时间，CNF vars / clauses / literals 变化。
- priority registration：terms、bits、lits、unique_lits、duplicate_lits。
- native decision path：seed、decide calls、returns、fallbacks、positive / negative phase。
- external path 旧指标：observed_var、cb_decide、notify 计数应保持为 0。

相关文件：

- `src/solver/bv/bv_bitblast_solver.cpp`
- `src/solver/bv/bv_bitblast_solver.h`
- `src/sat/cadical.cpp`
- `test/perf/solve_before/run_solve_before_perf.py`
- `test/perf/solve_before/run_solve_before_seed_sweep.py`
- `test/perf/solve_before/README.md`

## 验证结果

已验证的关键结果：

- `meson compile -C build -j4` 通过。
- `meson test -C build sat_cadical_decision_priority --print-errorlogs` 通过。
- `meson test -C build parser_solve_before.smt2 parser_solve_before_chain.smt2 parser_solve_before_cycle.smt2 parser_solve_before_self.smt2 --print-errorlogs` 通过。
- seed sweep 通过关键指标：
  - 32 个 seed 得到 32 个不同 `A/B` 模型。
  - 同一 seed 重跑可复现。
  - bit one fraction 约为 `A=0.539`、`B=0.461`。

验证命令里编译必须遵守本 checkout 的限制：使用 `-j4` 或更低。

## 已知边界

这个方案解决的是 SAT decision-priority 后端性能和 seed 可观察性，不解决所有 `solve-before` 成本。

仍然存在的主要成本：

- priority term 需要 materialize。若用户对本来不参与约束的复杂表达式做 `set-solve-before`，仍可能额外 bitblast / encode，导致 CNF 膨胀。
- `solve-before` 仍可能改变 CaDiCaL 原生 branching heuristic 的路径。某些实例可能更快，某些实例可能更慢。
- 统计项目前偏诊断用途，长期可以收敛成更少的稳定指标。
- vendored CaDiCaL patch 维护面比单纯 Bitwuzla wrapper 改动更大，但这是去掉 external-propagator 成本并保持 priority vars 可决策所需的代价。

## 后续建议

短期建议：

1. 保留 native CaDiCaL priority 方向。
2. 保留 seed sweep 为 opt-in 性能/随机性 smoke，不放进默认 regress。
3. 保留 external callback 指标一段时间，用于证明 native 路径没有退回 external propagator。
4. 对大规模真实 case 分别看 materialization、registration、search 三类指标，不要只看总时间。

中期可以考虑：

1. 精简诊断统计，只保留稳定、有解释力的少数指标。
2. 研究非 materializing 的 solve-before hint，避免对无关复杂 priority term 制造额外 CNF。
3. 把 vendored CaDiCaL patch 以更清晰的 overlay / patch 形式维护，降低后续升级成本。

## 一句话版本

最终方案是把 `solve-before` 从 external propagator callback 模拟，升级为 CaDiCaL 内部 native decision-priority：priority vars 在 CaDiCaL 内部 freeze/melt，决策时按 tier 和 seed-derived key/phase 选择，并在 active priority 存在时跳过 lucky phase，保证 priority 和 seed 真正进入 CDCL decision path。
