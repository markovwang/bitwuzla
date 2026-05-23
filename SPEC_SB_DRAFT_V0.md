# Solve-Before Draft Spec V0

## Summary

This draft proposes a minimal Bitwuzla extension for solve-before behavior.
The feature is intentionally scoped as a SAT decision-priority hint, not as
full SystemVerilog randomized constraint distribution semantics.

For an ordering such as:

```text
solve A before B
```

Bitwuzla should, after bit-blasting, ask the SAT backend to prefer decisions
on SAT variables corresponding to bits of `A` before decisions on bits of `B`.

This is a heuristic search-order feature. It does not guarantee uniform
sampling of `A`, does not compute projected feasible domains, and does not
commit to SystemVerilog-compatible randomization semantics.

## SAT Solver API

Extend the internal SAT abstraction with optional decision-priority support:

```cpp
virtual bool supports_decision_priority() const { return false; }

virtual void add_decision_priority_lit(int32_t lit, uint32_t priority)
{
  (void) lit;
  (void) priority;
}
```

For the CaDiCaL backend, implement this with
`CaDiCaL::ExternalPropagator::cb_decide()`.

Expected behavior:

- Lower numeric priority values are selected first.
- Within one priority bucket, implementation may preserve insertion order or
  use a simple randomized order.
- If no priority literal is available, `cb_decide()` returns `0` and CaDiCaL
  falls back to its normal decision heuristic.
- Priority literals must be registered as observed variables with CaDiCaL.

The initial implementation may support only CaDiCaL. Other SAT backends should
either report unsupported or ignore the hint only if explicitly configured to
do so.

## Bitwuzla API

Use a term-level API internally. Prefer a priority-oriented representation:

```cpp
void BvBitblastSolver::set_decision_priority(const Node& term,
                                             uint32_t priority);
```

A solve-before pair can lower to:

```cpp
set_decision_priority(A, 0);
set_decision_priority(B, 1);
```

Minimal public API:

```c
void bitwuzla_set_solve_before(Bitwuzla *bitwuzla,
                               BitwuzlaTerm before,
                               BitwuzlaTerm after);
```

Suggested C++ wrapper:

```cpp
void Bitwuzla::set_solve_before(const Term& before, const Term& after);
```

Validation rules:

- `before` and `after` must be Boolean or bit-vector terms.
- Both terms must belong to the same term manager and solver context.
- Calls must happen before `check_sat`.
- V0 supports only the bit-blast BV path with CaDiCaL.
- Unsupported configurations should fail loudly for prototype correctness.

## SMT-LIB Extension

SMT-LIB has no standard solve-before command. Add an explicit Bitwuzla-specific
command:

```smt2
(set-solve-before A B)
```

Example:

```smt2
(set-logic QF_BV)

(declare-fun A () (_ BitVec 8))
(declare-fun B () (_ BitVec 8))

(assert (= B (bvadd A #x01)))

(set-solve-before A B)

(check-sat)
(get-model)
```

The command should be parsed as a solver directive, not as an assertion. It
refers to already-declared terms.

## Implementation Notes

The main integration challenge is mapping high-level terms to SAT literals:

1. Ensure prioritized terms are bit-blasted before the SAT solve starts.
2. Retrieve their AIG bits via the BV bitblaster.
3. Ignore constant bits.
4. Use each non-constant AIG bit ID as the SAT literal/variable.
5. Register those variables with the CaDiCaL external propagator.
6. Let the propagator return unassigned priority literals from `cb_decide()`.

This preserves SAT correctness because CaDiCaL may reject already-assigned or
fixed literals and fall back to its ordinary heuristic. Conflicts still cause
normal CDCL backtracking and learning.

## Documented Semantics

Document the feature as follows:

```text
(set-solve-before A B) is a decision-priority hint.
For Boolean and bit-vector terms, Bitwuzla asks the SAT solver to branch on
bits of A before bits of B when possible.
It does not implement SystemVerilog solve-before distribution semantics.
It does not guarantee uniform sampling.
It does not force A to be independent of B.
It is initially supported only for the CaDiCaL bit-blast backend.
```

## Non-Goals For V0

- Exact SystemVerilog randomized solve-before semantics.
- Projected model counting or uniform sampling over `A`.
- Support for non-bit-blasted theories.
- Portable implementation across every SAT backend.
- SMT-LIB standard compatibility.

## Final V0 Design And Implementation Notes

This section records the concrete design implemented in this branch. It is
deliberately more implementation-oriented than the sketch above.

### Motivation

SystemVerilog `solve a before b` affects randomized constraint solving by
making the solver conceptually choose values for `a` before values for `b`.
That wording is easy to misunderstand when mapped to SMT. In SMT, the formula
is solved as one conjunction. If `a` and `b` are related by constraints, solving
`a` independently can choose a value that makes `b` impossible even though
another value for `a` would be satisfiable. A sound SMT solver cannot simply
commit to `a` without considering the whole formula unless it is willing to
backtrack.

For this reason the implemented feature is not a semantic decomposition of the
SMT problem. It is a decision-order directive. The solver still solves the full
formula, learns clauses normally, backtracks normally, and returns exactly the
same SAT/UNSAT result it would return without the directive. The directive only
asks the SAT backend to prefer decisions on bits associated with earlier terms
before decisions on bits associated with later terms.

### User-Facing SMT2 Extension

The SMT2 command is:

```smt2
(set-solve-before <before-term> <after-term>)
```

The command is a Bitwuzla extension, not an SMT-LIB standard command. It is a
solver directive, not an assertion. It does not change the Boolean formula. It
adds an ordering edge to a separate solve-before graph.

Example:

```smt2
(set-logic QF_BV)

(declare-fun A () (_ BitVec 8))
(declare-fun B () (_ BitVec 8))

(assert (= B (bvadd A #x01)))
(assert (bvult B #x10))

(set-solve-before A B)

(check-sat)
(get-model)
```

Accepted arguments:

- Boolean terms.
- Bit-vector terms.

Rejected arguments:

- Array terms.
- Function terms.
- Floating-point terms before they are represented as bit-vector leaves.
- Any other non-Boolean, non-bit-vector term.
- A self-edge such as `(set-solve-before A A)`.

The command must be issued before solving. The C++ API enforces that
solve-before directives are registered before the first `check_sat`.

### Term-Level API

The C++ API entry point is:

```cpp
void Bitwuzla::set_solve_before(const Term& before, const Term& after);
```

It validates:

- The solver context exists.
- No SAT call has happened yet.
- Both terms are non-null.
- Both terms are Boolean or bit-vector terms.
- Both terms belong to the same term manager as the solver.
- `before != after`.

After validation, it records the edge in `SolvingContext`:

```cpp
d_ctx->add_solve_before(*before.d_node, *after.d_node);
```

### Graph Model

The internal representation is a directed graph over `Node` objects:

```text
before -> after
```

The intended meaning is:

```text
the SAT variables produced from `before` should be considered before the SAT
variables produced from `after`
```

Duplicate edges are ignored. Self-edges are rejected when inserted and checked
again when tiers are computed.

### Tier Calculation

Before preprocessing and solving, `SolvingContext::solve()` calls:

```cpp
compute_solve_before_tiers();
```

The algorithm is a topological traversal:

1. Collect every endpoint of every solve-before edge.
2. Assign each endpoint a compact integer index.
3. Build successor lists and indegree counts.
4. Initialize all source nodes with tier `0`.
5. Pop nodes in topological order.
6. For every edge `u -> v`, update:

   ```text
   tier[v] = max(tier[v], tier[u] + 1)
   ```

7. If not every node is visited, the graph has a cycle and solving fails with:

   ```text
   cyclic solve-before dependency
   ```

This gives longest-path tiers. In a graph:

```text
A -> B
B -> C
A -> D
```

the tiers are:

```text
A: 0
B: 1
D: 1
C: 2
```

Lower numeric tiers are higher decision priority. Terms in the same tier have
no ordering relation in V0. Their relative order is intentionally unspecified.

### Why Tiers Instead Of Pairwise Callbacks

CaDiCaL's external decision callback chooses one literal at a time. It does not
know about high-level SMT terms. A pairwise graph such as `A before B` therefore
has to be lowered into a total or partial order over concrete SAT decision
candidates. Tiers are a minimal representation of the partial order:

- They preserve transitive solve-before structure.
- They detect cycles cleanly.
- They allow unrelated terms to share a priority level.
- They avoid creating a full total order when the user only specified a partial
  order.

### SAT Abstraction API

The internal SAT abstraction now has optional decision-priority hooks:

```cpp
virtual bool supports_decision_priority() const { return false; }
virtual void clear_decision_priority() {}
virtual void add_decision_priority_lit(int32_t lit, uint32_t priority);
```

The default implementation reports no support and ignores the callbacks. The
BV bitblast solver only uses the hooks when there are non-empty solve-before
tiers. If the current SAT backend does not support priority hints and tiers are
present, Bitwuzla fails loudly:

```text
solve-before decision priority is only supported by CaDiCaL
```

This keeps the V0 behavior explicit. A silent ignore would make experiments
misleading because users could believe solve-before was active when it was not.

### Lowering SMT Terms To SAT Literals

The solve-before graph is term-level, but CaDiCaL only sees integer SAT
literals. The lowering happens in `BvBitblastSolver` before calling
`SatSolver::solve()`:

1. Clear previous decision-priority registrations.
2. If there are no tiers, return immediately.
3. Check that the selected SAT solver supports decision-priority hints.
4. For each `(term, tier)` pair:
   - Bitblast the term on demand.
   - Read the AIG bits for the term.
   - Skip constant bits.
   - Encode each non-constant AIG bit into CNF.
   - Register each resulting SAT literal with the tier.

The on-demand bitblast step is important. A solve-before term may not have been
directly asserted. Without bitblasting it here, the term could have no cached
AIG bits and the directive would silently do nothing.

The encoding step is also important. CaDiCaL can only prioritize variables that
exist in the SAT instance. Encoding the AIG bit ensures the SAT literal is
materialized before it is given to the decision-priority interface.

### CaDiCaL ExternalPropagator Strategy

The CaDiCaL wrapper connects a small `ExternalPropagator` implementation. It
does not propagate clauses. It only uses the decision callback:

```cpp
int cb_decide() override;
```

The propagator stores:

- A map from priority tier to registered literals.
- A sorted list of active priority tiers.
- A set of SAT variables currently known to be assigned.
- A per-decision-level assignment stack used to remove assignments on
  backtrack.

When CaDiCaL asks for an external decision, the callback scans tiers in
ascending numeric order and returns the first literal whose variable is not
currently assigned. If every registered priority variable is already assigned,
it returns `0`, which tells CaDiCaL to use its normal internal decision
heuristic.

No external clauses are produced:

```cpp
bool cb_has_external_clause() override { return false; }
int cb_add_external_clause_lit() override { return 0; }
```

Model checking also accepts the model without adding constraints:

```cpp
bool cb_check_found_model(...) override { return true; }
```

This keeps the feature a pure branching hint. It cannot change SAT/UNSAT
correctness because it does not add, remove, or rewrite clauses.

### Backtracking And Assignment Tracking

The decision callback must not repeatedly return a variable that CaDiCaL has
already assigned. The propagator tracks assignments through CaDiCaL callbacks:

- `notify_assignment(lit, is_fixed)` records assigned variables.
- `notify_new_decision_level()` pushes a new level.
- `notify_backtrack(new_level)` removes assignments above the backtrack level.

The tracking is variable-based, not literal-sign-based. If `x` is assigned,
both `x` and `not x` are unavailable for further decisions.

Fixed assignments are stored at level 0. Non-fixed assignments are stored at the
current decision level so they can be erased correctly during backtracking.

### Soundness Boundary

The implementation is sound because solve-before is not encoded as a constraint.
It only influences which unassigned SAT variable CaDiCaL branches on next. CDCL
still owns:

- Boolean propagation.
- Conflict analysis.
- Clause learning.
- Backjumping.
- Restart policy.
- Final SAT/UNSAT decision.

If a preferred `A` bit decision later makes `B` impossible, CaDiCaL can learn a
conflict clause and backtrack. This is exactly why the feature is described as
a decision-priority hint and not as independent solving of `A`.

### Behavior With Cycles

Cycles are rejected before preprocessing:

```smt2
(set-solve-before A B)
(set-solve-before B A)
```

This produces:

```text
[bzla] error: cyclic solve-before dependency
```

Cycles are rejected because no finite priority assignment can satisfy all
strict ordering edges in the cycle. Weakening cycles into equal tiers would make
the command ambiguous and would hide user mistakes.

### Behavior With Islands

The implementation does not introduce a separate island solver. Disconnected
components in the solve-before graph are simply independent components of the
same priority-tier calculation. If two components both contain source nodes,
those source nodes all receive tier `0`.

For example:

```text
A -> B
C -> D
```

gives:

```text
A: 0
C: 0
B: 1
D: 1
```

This matches V0's scope: one SAT solve, with a partial order over priority
terms. It does not split the formula into separately solved subproblems.

### Current File-Level Implementation

Parser and tokens:

- `src/parser/smt2/token.h`
  Adds `SET_SOLVE_BEFORE`.
- `src/parser/smt2/token.cpp`
  Prints the token as `set-solve-before`.
- `src/parser/smt2/symbol_table.cpp`
  Registers the SMT2 command keyword.
- `src/parser/smt2/parser.h`
  Declares `parse_command_set_solve_before()`.
- `src/parser/smt2/parser.cpp`
  Dispatches and parses the command, validates arity and term sorts, rejects
  self-edges, and calls the C++ API.

C++ API:

- `include/bitwuzla/cpp/bitwuzla.h`
  Declares `Bitwuzla::set_solve_before()`.
- `src/api/cpp/bitwuzla.cpp`
  Implements validation and forwards accepted edges to `SolvingContext`.

Solving context:

- `src/solving_context.h`
  Stores solve-before edges and computed term tiers.
- `src/solving_context.cpp`
  Adds edges, computes tiers, rejects cycles, and computes tiers before
  preprocessing/solving.

Solver engine and BV solver:

- `src/solver/solver_engine.cpp`
  Passes computed tiers to the BV solver before SAT solving.
- `src/solver/bv/bv_solver.h`
  Adds a forwarding API for term decision priorities.
- `src/solver/bv/bv_solver.cpp`
  Forwards priority terms to the bitblast solver.
- `src/solver/bv/bv_bitblast_solver.h`
  Stores decision-priority terms.
- `src/solver/bv/bv_bitblast_solver.cpp`
  Bitblasts, encodes, and registers tiered term bits before SAT solving.

SAT abstraction and CaDiCaL:

- `src/sat/sat_solver.h`
  Adds optional decision-priority methods.
- `src/sat/cadical.h`
  Reports support for decision priority and stores the connected propagator.
- `src/sat/cadical.cpp`
  Implements the priority-only `ExternalPropagator`, registers observed
  variables, and serves decisions from lower tiers first.

Tests:

- `test/regress/parser/solve_before.smt2`
  Basic accepted command.
- `test/regress/parser/solve_before_chain.smt2`
  Transitive chain accepted by the parser and tiering path.
- `test/regress/parser/solve_before_cycle.smt2`
  Cycle rejection.
- `test/regress/parser/solve_before_self.smt2`
  Self-edge rejection.
- `test/regress/meson.build`
  Registers the new parser regressions.

### Known Limitations

- V0 supports only the CaDiCaL backend for actual decision-priority behavior.
- The command is a Bitwuzla SMT2 extension and is not portable SMT-LIB.
- It is not SystemVerilog randomization distribution semantics.
- It does not perform projected domain analysis.
- It does not guarantee uniform sampling of earlier terms.
- It does not split independent islands into separate SAT calls.
- Relative order inside one tier is currently implementation-defined.
- A solve-before term that rewrites to constants may contribute no SAT
  decisions, which is expected.
- Preprocessing may change the formula before the SAT solve. The current V0
  computes tiers before preprocessing, then registers prioritized terms in the
  bitblast solver. This keeps the prototype simple but may later need tighter
  integration with rewritten representatives if solve-before becomes a
  supported public feature.

### Future Work

- Add a C API equivalent if this feature should be exposed beyond the C++
  wrapper and SMT2 parser.
- Add an option controlling unsupported backend behavior, for example hard
  error versus warning and ignore.
- Preserve deterministic intra-tier ordering by storing tiered terms in a
  stable vector instead of an unordered map.
- Decide how solve-before directives interact with incremental solving,
  `push`, `pop`, and `reset-assertions`.
- Add instrumentation to confirm CaDiCaL decision callbacks are selecting the
  expected variables in controlled tests.
- Consider a stronger semantics mode based on projected model enumeration or
  sampling if SystemVerilog-compatible randomized behavior becomes the target.
