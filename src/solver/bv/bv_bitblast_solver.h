/***
 * Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
 *
 * Copyright (C) 2022 by the authors listed in the AUTHORS file at
 * https://github.com/bitwuzla/bitwuzla/blob/main/AUTHORS
 *
 * This file is part of Bitwuzla under the MIT license. See COPYING for more
 * information at https://github.com/bitwuzla/bitwuzla/blob/main/COPYING
 */

#ifndef BZLA_SOLVER_BV_BV_BITBLAST_SOLVER_H_INCLUDED
#define BZLA_SOLVER_BV_BV_BITBLAST_SOLVER_H_INCLUDED

#include <unordered_map>

#include "backtrack/vector.h"
#include "bitblast/aig/aig_cnf.h"
#include "sat/sat_solver.h"
#include "solver/bv/aig_bitblaster.h"
#include "solver/bv/bv_solver_interface.h"
#include "solver/solver.h"
#include "util/statistics.h"

namespace bzla::bv {

class BvSolver;

class BvBitblastSolver : public Solver, public BvSolverInterface
{
 public:
  BvBitblastSolver(Env& env, SolverState& state);
  ~BvBitblastSolver();

  Result solve() override;

  void register_assertion(const Node& assertion,
                          bool top_level,
                          bool is_lemma) override;

  /** Set term-level decision-priority tiers. */
  void set_decision_priority_terms(
      const std::unordered_map<Node, uint32_t>& tiers);

  /** Query value of leaf node. */
  Node value(const Node& term) override;

  /** Get unsat core of last solve() call. */
  void unsat_core(std::vector<Node>& core) const override;

  /** Get AIG bit-blaster instance. */
  AigBitblaster& bitblaster() { return d_bitblaster; }

  /** Get statistics. */
  const auto& statistics() const { return d_stats; }

 private:
  /** Update AIG and CNF statistics. */
  void update_statistics();

  /** Sync decision-priority statistics from the SAT solver. */
  void update_decision_priority_sat_statistics();

  /** Register bit-blasted priority terms with SAT solver. */
  void register_decision_priorities();

  /** Sat interface used for d_cnf_encoder. */
  class BitblastSatSolver;

  /** The current set of assertions. */
  backtrack::vector<Node> d_assertions;
  /** The current set of assumptions. */
  backtrack::vector<Node> d_assumptions;

  /** Term-level decision-priority tiers. */
  std::unordered_map<Node, uint32_t> d_decision_priority_terms;

  /** AIG bit-blaster. */
  AigBitblaster d_bitblaster;

  /** CNF encoder for AIGs. */
  std::unique_ptr<bitblast::AigCnfEncoder> d_cnf_encoder;
  /** SAT solver used for solving bit-blasted formula. */
  std::unique_ptr<sat::SatSolver> d_sat_solver;
  /** SAT solver interface for CNF encoder, which wraps `d_sat_solver`. */
  std::unique_ptr<BitblastSatSolver> d_bitblast_sat_solver;
  /** Result of last solve() call. */
  Result d_last_result;

  struct Statistics
  {
    Statistics(util::Statistics& stats, const std::string& prefix);
    util::TimerStatistic& time_sat;
    util::TimerStatistic& time_bitblast;
    util::TimerStatistic& time_encode;
    util::TimerStatistic& time_decision_priority_register;
    util::TimerStatistic& time_decision_priority_bitblast;
    util::TimerStatistic& time_decision_priority_encode;
    uint64_t& num_aig_ands;
    uint64_t& num_aig_consts;
    uint64_t& num_aig_shared;
    uint64_t& num_cnf_vars;
    uint64_t& num_cnf_clauses;
    uint64_t& num_cnf_literals;
    uint64_t& num_decision_priority_register_rounds;
    uint64_t& num_decision_priority_terms;
    uint64_t& num_decision_priority_bits;
    uint64_t& num_decision_priority_const_bits;
    uint64_t& num_decision_priority_lits;
    uint64_t& num_decision_priority_sat_add_lit_calls;
    uint64_t& num_decision_priority_sat_add_lit_duplicates;
    uint64_t& num_decision_priority_sat_unique_lits;
    uint64_t& num_decision_priority_sat_max_priority_buckets;
    uint64_t& num_decision_priority_sat_native_seed;
    uint64_t& num_decision_priority_sat_native_decide_calls;
    uint64_t& num_decision_priority_sat_native_decide_returns;
    uint64_t& num_decision_priority_sat_native_decide_fallbacks;
    uint64_t& num_decision_priority_sat_native_decide_positive;
    uint64_t& num_decision_priority_sat_native_decide_negative;
    uint64_t& num_decision_priority_sat_observed_var_calls;
    uint64_t& num_decision_priority_sat_cb_decide_calls;
    uint64_t& num_decision_priority_sat_cb_decide_returns;
    uint64_t& num_decision_priority_sat_cb_decide_fallbacks;
    uint64_t& num_decision_priority_sat_cb_decide_scanned_lits;
    uint64_t& num_decision_priority_sat_cb_decide_max_scan;
    uint64_t& num_decision_priority_sat_notify_assignment_calls;
    uint64_t& num_decision_priority_sat_notify_backtrack_calls;
    uint64_t& num_decision_priority_sat_notify_new_decision_level_calls;
    uint64_t& num_decision_priority_sat_notify_backtrack_erased_vars;
  } d_stats;
};

}  // namespace bzla::bv

#endif
