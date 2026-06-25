/***
 * Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
 *
 * Copyright (C) 2022 by the authors listed in the AUTHORS file at
 * https://github.com/bitwuzla/bitwuzla/blob/main/AUTHORS
 *
 * This file is part of Bitwuzla under the MIT license. See COPYING for more
 * information at https://github.com/bitwuzla/bitwuzla/blob/main/COPYING
 */

#include "solver/bv/bv_bitblast_solver.h"

#include "bv/bitvector.h"
#include "env.h"
#include "node/node_manager.h"
#include "node/node_utils.h"
#include "sat/sat_solver_factory.h"
#include "solver/bv/bv_solver.h"
#include "util/exceptions.h"

namespace bzla::bv {

using namespace bzla::node;

/** Sat solver wrapper for AIG encoder. */
class BvBitblastSolver::BitblastSatSolver : public bitblast::SatInterface
{
 public:
  BitblastSatSolver(sat::SatSolver& solver) : d_solver(solver) {}

  void add(int64_t lit) override { d_solver.add(lit); }

  void add_clause(const std::initializer_list<int64_t>& literals) override
  {
    for (int64_t lit : literals)
    {
      d_solver.add(lit);
    }
    d_solver.add(0);
  }

  bool value(int64_t lit) override
  {
    return d_solver.value(lit) == 1 ? true : false;
  }

 private:
  sat::SatSolver& d_solver;
};

/* --- BvBitblastSolver public ---------------------------------------------- */

BvBitblastSolver::BvBitblastSolver(Env& env, SolverState& state)
    : Solver(env, state),
      d_assertions(state.backtrack_mgr()),
      d_assumptions(state.backtrack_mgr()),
      d_last_result(Result::UNKNOWN),
      d_stats(env.statistics(), "solver::bv::bitblast::")
{
  d_sat_solver.reset(sat::new_sat_solver(env.options()));
  d_bitblast_sat_solver.reset(new BitblastSatSolver(*d_sat_solver));
  d_cnf_encoder.reset(new bitblast::AigCnfEncoder(*d_bitblast_sat_solver));
}

BvBitblastSolver::~BvBitblastSolver() {}

Result
BvBitblastSolver::solve()
{
  d_sat_solver->configure_terminator(d_env.terminator());

  if (!d_assertions.empty())
  {
    util::Timer timer(d_stats.time_encode);
    for (const Node& assertion : d_assertions)
    {
      const auto& bits = d_bitblaster.bits(assertion);
      assert(!bits.empty());
      d_cnf_encoder->encode(bits[0], true);
    }
    d_assertions.clear();
  }

  for (const Node& assumption : d_assumptions)
  {
    const auto& bits = d_bitblaster.bits(assumption);
    assert(!bits.empty());
    util::Timer timer(d_stats.time_encode);
    d_cnf_encoder->encode(bits[0], false);
    d_sat_solver->assume(bits[0].get_id());
  }

  register_decision_priorities();

  // Update CNF statistics
  update_statistics();

  d_solver_state.print_statistics();
  util::Timer timer(d_stats.time_sat);
  d_last_result = d_sat_solver->solve();
  update_decision_priority_sat_statistics();

  return d_last_result;
}

void
BvBitblastSolver::set_decision_priority_terms(
    const std::unordered_map<Node, uint32_t>& tiers)
{
  d_decision_priority_terms = tiers;
}

void
BvBitblastSolver::register_assertion(const Node& assertion,
                                     bool top_level,
                                     bool is_lemma)
{
  // If unsat cores are enabled, all assertions are assumptions except lemmas.
  if (d_env.options().produce_unsat_cores() && !is_lemma)
  {
    top_level = false;
  }

  if (!top_level)
  {
    d_assumptions.push_back(assertion);
  }
  else
  {
    d_assertions.push_back(assertion);
  }

  {
    util::Timer timer(d_stats.time_bitblast);
    d_bitblaster.bitblast(assertion);
  }

  // Update AIG statistics
  update_statistics();
}

Node
BvBitblastSolver::value(const Node& term)
{
  assert(BvSolver::is_leaf(term));
  assert(term.type().is_bool() || term.type().is_bv());

  const auto& bits = d_bitblaster.bits(term);
  const Type& type = term.type();
  NodeManager& nm  = d_env.nm();

  // Return default value if not bit-blasted
  if (bits.empty())
  {
    return utils::mk_default_value(nm, type);
  }

  if (type.is_bool())
  {
    return nm.mk_value(d_cnf_encoder->value(bits[0]) == 1);
  }

  BitVector val(type.bv_size());
  for (size_t i = 0, size = bits.size(); i < size; ++i)
  {
    val.set_bit(size - 1 - i, d_cnf_encoder->value(bits[i]) == 1);
  }
  return nm.mk_value(val);
}

void
BvBitblastSolver::unsat_core(std::vector<Node>& core) const
{
  assert(d_last_result == Result::UNSAT);
  assert(d_env.options().produce_unsat_cores());

  for (const Node& assumption : d_assumptions)
  {
    const auto& bits = d_bitblaster.bits(assumption);
    assert(bits.size() == 1);
    if (d_sat_solver->failed(bits[0].get_id()))
    {
      core.push_back(assumption);
    }
  }
}

/* --- BvBitblastSolver private --------------------------------------------- */

void
BvBitblastSolver::register_decision_priorities()
{
  d_sat_solver->clear_decision_priority();
  if (d_decision_priority_terms.empty())
  {
    return;
  }
  if (!d_sat_solver->supports_decision_priority())
  {
    throw Error("solve-before decision priority is only supported by CaDiCaL");
  }

  ++d_stats.num_decision_priority_register_rounds;
  util::Timer timer(d_stats.time_decision_priority_register);
  for (const auto& [term, priority] : d_decision_priority_terms)
  {
    ++d_stats.num_decision_priority_terms;
    {
      util::Timer timer_bitblast(d_stats.time_decision_priority_bitblast);
      d_bitblaster.bitblast(term);
    }
    const auto& bits = d_bitblaster.bits(term);
    d_stats.num_decision_priority_bits += bits.size();
    for (const auto& bit : bits)
    {
      if (bit.is_true() || bit.is_false())
      {
        ++d_stats.num_decision_priority_const_bits;
        continue;
      }
      {
        util::Timer timer_encode(d_stats.time_encode);
        util::Timer timer_priority_encode(
            d_stats.time_decision_priority_encode);
        d_cnf_encoder->encode(bit, false);
      }
      d_sat_solver->add_decision_priority_lit(bit.get_id(), priority);
      ++d_stats.num_decision_priority_lits;
    }
  }
}

void
BvBitblastSolver::update_decision_priority_sat_statistics()
{
  const auto stats = d_sat_solver->decision_priority_stats();
  d_stats.num_decision_priority_sat_add_lit_calls = stats.add_lit_calls;
  d_stats.num_decision_priority_sat_add_lit_duplicates =
      stats.add_lit_duplicates;
  d_stats.num_decision_priority_sat_unique_lits = stats.unique_lits;
  d_stats.num_decision_priority_sat_max_priority_buckets =
      stats.max_priority_buckets;
  d_stats.num_decision_priority_sat_native_seed = stats.native_seed;
  d_stats.num_decision_priority_sat_native_decide_calls =
      stats.native_decide_calls;
  d_stats.num_decision_priority_sat_native_decide_returns =
      stats.native_decide_returns;
  d_stats.num_decision_priority_sat_native_decide_fallbacks =
      stats.native_decide_fallbacks;
  d_stats.num_decision_priority_sat_native_decide_positive =
      stats.native_decide_positive;
  d_stats.num_decision_priority_sat_native_decide_negative =
      stats.native_decide_negative;
  d_stats.num_decision_priority_sat_observed_var_calls =
      stats.observed_var_calls;
  d_stats.num_decision_priority_sat_cb_decide_calls = stats.cb_decide_calls;
  d_stats.num_decision_priority_sat_cb_decide_returns =
      stats.cb_decide_returns;
  d_stats.num_decision_priority_sat_cb_decide_fallbacks =
      stats.cb_decide_fallbacks;
  d_stats.num_decision_priority_sat_cb_decide_scanned_lits =
      stats.cb_decide_scanned_lits;
  d_stats.num_decision_priority_sat_cb_decide_max_scan =
      stats.cb_decide_max_scan;
  d_stats.num_decision_priority_sat_notify_assignment_calls =
      stats.notify_assignment_calls;
  d_stats.num_decision_priority_sat_notify_backtrack_calls =
      stats.notify_backtrack_calls;
  d_stats.num_decision_priority_sat_notify_new_decision_level_calls =
      stats.notify_new_decision_level_calls;
  d_stats.num_decision_priority_sat_notify_backtrack_erased_vars =
      stats.notify_backtrack_erased_vars;
}

void
BvBitblastSolver::update_statistics()
{
  d_stats.num_aig_ands     = d_bitblaster.num_aig_ands();
  d_stats.num_aig_consts   = d_bitblaster.num_aig_consts();
  d_stats.num_aig_shared   = d_bitblaster.num_aig_shared();
  auto& cnf_stats          = d_cnf_encoder->statistics();
  d_stats.num_cnf_vars     = cnf_stats.num_vars;
  d_stats.num_cnf_clauses  = cnf_stats.num_clauses;
  d_stats.num_cnf_literals = cnf_stats.num_literals;
}

BvBitblastSolver::Statistics::Statistics(util::Statistics& stats,
                                         const std::string& prefix)
    : time_sat(
          stats.new_stat<util::TimerStatistic>(prefix + "sat::time_solve")),
      time_bitblast(
          stats.new_stat<util::TimerStatistic>(prefix + "aig::time_bitblast")),
      time_encode(
          stats.new_stat<util::TimerStatistic>(prefix + "cnf::time_encode")),
      time_decision_priority_register(
          stats.new_stat<util::TimerStatistic>(
              prefix + "decision_priority::time_register")),
      time_decision_priority_bitblast(
          stats.new_stat<util::TimerStatistic>(
              prefix + "decision_priority::time_bitblast")),
      time_decision_priority_encode(
          stats.new_stat<util::TimerStatistic>(
              prefix + "decision_priority::time_encode")),
      num_aig_ands(stats.new_stat<uint64_t>(prefix + "aig::num_ands")),
      num_aig_consts(stats.new_stat<uint64_t>(prefix + "aig::num_consts")),
      num_aig_shared(stats.new_stat<uint64_t>(prefix + "aig::num_shared")),
      num_cnf_vars(stats.new_stat<uint64_t>(prefix + "cnf::num_vars")),
      num_cnf_clauses(stats.new_stat<uint64_t>(prefix + "cnf::num_clauses")),
      num_cnf_literals(stats.new_stat<uint64_t>(prefix + "cnf::num_literals")),
      num_decision_priority_register_rounds(stats.new_stat<uint64_t>(
          prefix + "decision_priority::register_rounds")),
      num_decision_priority_terms(
          stats.new_stat<uint64_t>(prefix + "decision_priority::terms")),
      num_decision_priority_bits(
          stats.new_stat<uint64_t>(prefix + "decision_priority::bits")),
      num_decision_priority_const_bits(
          stats.new_stat<uint64_t>(prefix + "decision_priority::const_bits")),
      num_decision_priority_lits(
          stats.new_stat<uint64_t>(prefix + "decision_priority::lits")),
      num_decision_priority_sat_add_lit_calls(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::add_lit_calls")),
      num_decision_priority_sat_add_lit_duplicates(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::add_lit_duplicates")),
      num_decision_priority_sat_unique_lits(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::unique_lits")),
      num_decision_priority_sat_max_priority_buckets(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::max_priority_buckets")),
      num_decision_priority_sat_native_seed(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::native_seed")),
      num_decision_priority_sat_native_decide_calls(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::native_decide_calls")),
      num_decision_priority_sat_native_decide_returns(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::native_decide_returns")),
      num_decision_priority_sat_native_decide_fallbacks(
          stats.new_stat<uint64_t>(
              prefix + "decision_priority::sat::native_decide_fallbacks")),
      num_decision_priority_sat_native_decide_positive(
          stats.new_stat<uint64_t>(
              prefix + "decision_priority::sat::native_decide_positive")),
      num_decision_priority_sat_native_decide_negative(
          stats.new_stat<uint64_t>(
              prefix + "decision_priority::sat::native_decide_negative")),
      num_decision_priority_sat_observed_var_calls(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::observed_var_calls")),
      num_decision_priority_sat_cb_decide_calls(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::cb_decide_calls")),
      num_decision_priority_sat_cb_decide_returns(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::cb_decide_returns")),
      num_decision_priority_sat_cb_decide_fallbacks(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::cb_decide_fallbacks")),
      num_decision_priority_sat_cb_decide_scanned_lits(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::cb_decide_scanned_lits")),
      num_decision_priority_sat_cb_decide_max_scan(stats.new_stat<uint64_t>(
          prefix + "decision_priority::sat::cb_decide_max_scan")),
      num_decision_priority_sat_notify_assignment_calls(
          stats.new_stat<uint64_t>(
              prefix + "decision_priority::sat::notify_assignment_calls")),
      num_decision_priority_sat_notify_backtrack_calls(
          stats.new_stat<uint64_t>(
              prefix + "decision_priority::sat::notify_backtrack_calls")),
      num_decision_priority_sat_notify_new_decision_level_calls(
          stats.new_stat<uint64_t>(
              prefix
              + "decision_priority::sat::notify_new_decision_level_calls")),
      num_decision_priority_sat_notify_backtrack_erased_vars(
          stats.new_stat<uint64_t>(
              prefix
              + "decision_priority::sat::notify_backtrack_erased_vars"))
{
}

}  // namespace bzla::bv
