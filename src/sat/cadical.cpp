/***
 * Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
 *
 * Copyright (C) 2022 by the authors listed in the AUTHORS file at
 * https://github.com/bitwuzla/bitwuzla/blob/main/AUTHORS
 *
 * This file is part of Bitwuzla under the MIT license. See COPYING for more
 * information at https://github.com/bitwuzla/bitwuzla/blob/main/COPYING
 */

#include "sat/cadical.h"

#include <cassert>

namespace bzla::sat {

/* CadicalTerminator public ------------------------------------------------- */

CadicalTerminator::CadicalTerminator(bzla::Terminator* terminator)
    : CaDiCaL::Terminator(), d_terminator(terminator)
{
}

bool
CadicalTerminator::terminate()
{
  if (!d_terminator) return false;
  return d_terminator->terminate();
}

/* Cadical public ----------------------------------------------------------- */

Cadical::Cadical(uint32_t seed)
{
  d_solver.reset(new CaDiCaL::Solver());
  d_solver->set_decision_priority_seed(seed);
  d_solver->set("shrink", 0);
  d_solver->set("quiet", 1);
}

Cadical::~Cadical() {}

void
Cadical::add(int32_t lit)
{
  d_solver->add(lit);
}

void
Cadical::assume(int32_t lit)
{
  d_solver->assume(lit);
}

int32_t
Cadical::value(int32_t lit)
{
  int32_t val = d_solver->val(lit);
  if (val > 0) return 1;
  if (val < 0) return -1;
  return 0;
}

bool
Cadical::failed(int32_t lit)
{
  return d_solver->failed(lit);
}

int32_t
Cadical::fixed(int32_t lit)
{
  return d_solver->fixed(lit);
}

void
Cadical::clear_decision_priority()
{
  d_solver->clear_decision_priority();
}

void
Cadical::add_decision_priority_lit(int32_t lit, uint32_t priority)
{
  assert(lit);
  d_solver->add_decision_priority_lit(lit, priority);
}

DecisionPriorityStats
Cadical::decision_priority_stats() const
{
  const auto native = d_solver->decision_priority_stats();
  DecisionPriorityStats stats;
  stats.add_lit_calls       = native.add_lit_calls;
  stats.add_lit_duplicates  = native.add_lit_duplicates;
  stats.unique_lits         = native.unique_lits;
  stats.max_priority_buckets = native.max_priority_buckets;
  stats.native_seed         = native.seed;
  stats.native_decide_calls = native.decide_calls;
  stats.native_decide_returns = native.decide_returns;
  stats.native_decide_fallbacks = native.decide_fallbacks;
  stats.native_decide_positive = native.decide_positive;
  stats.native_decide_negative = native.decide_negative;
  return stats;
}

Result
Cadical::solve()
{
  int32_t res = d_solver->solve();
  if (res == 10) return Result::SAT;
  if (res == 20) return Result::UNSAT;
  return Result::UNKNOWN;
}

void
Cadical::configure_terminator(Terminator* terminator)
{
  d_term.reset(new CadicalTerminator(terminator));
  if (terminator)
  {
    d_solver->connect_terminator(d_term.get());
  }
  else
  {
    d_solver->disconnect_terminator();
  }
}

const char*
Cadical::get_version() const
{
  return d_solver->version();
}

/* -------------------------------------------------------------------------- */

}  // namespace bzla::sat
