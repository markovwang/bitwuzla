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

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bzla::sat {

class CadicalDecisionPropagator : public CaDiCaL::ExternalPropagator
{
 public:
  CadicalDecisionPropagator() { d_assignments.emplace_back(); }

  void clear()
  {
    d_buckets.clear();
    d_ordered_priorities.clear();
    d_assigned_vars.clear();
    d_assignments.clear();
    d_assignments.emplace_back();
  }

  void add_lit(int32_t lit, uint32_t priority)
  {
    assert(lit);
    auto& bucket = d_buckets[priority];
    if (std::find(bucket.begin(), bucket.end(), lit) == bucket.end())
    {
      bucket.push_back(lit);
    }
    if (std::find(
            d_ordered_priorities.begin(), d_ordered_priorities.end(), priority)
        == d_ordered_priorities.end())
    {
      d_ordered_priorities.push_back(priority);
      std::sort(d_ordered_priorities.begin(), d_ordered_priorities.end());
    }
  }

  void notify_assignment(int lit, bool is_fixed) override
  {
    int var = std::abs(lit);
    if (!d_assigned_vars.insert(var).second)
    {
      return;
    }
    if (d_assignments.empty())
    {
      d_assignments.emplace_back();
    }
    if (is_fixed)
    {
      d_assignments[0].push_back(var);
    }
    else
    {
      d_assignments.back().push_back(var);
    }
  }

  void notify_new_decision_level() override { d_assignments.emplace_back(); }

  void notify_backtrack(size_t new_level) override
  {
    size_t keep = new_level + 1;
    while (d_assignments.size() > keep)
    {
      for (int var : d_assignments.back())
      {
        d_assigned_vars.erase(var);
      }
      d_assignments.pop_back();
    }
    if (d_assignments.empty())
    {
      d_assignments.emplace_back();
    }
  }

  bool cb_check_found_model(const std::vector<int>& model) override
  {
    (void) model;
    return true;
  }

  int cb_decide() override
  {
    for (uint32_t priority : d_ordered_priorities)
    {
      for (int lit : d_buckets[priority])
      {
        if (d_assigned_vars.find(std::abs(lit)) == d_assigned_vars.end())
        {
          return lit;
        }
      }
    }
    return 0;
  }

  bool cb_has_external_clause() override { return false; }

  int cb_add_external_clause_lit() override { return 0; }

 private:
  std::unordered_map<uint32_t, std::vector<int>> d_buckets;
  std::vector<uint32_t> d_ordered_priorities;
  std::unordered_set<int> d_assigned_vars;
  std::vector<std::vector<int>> d_assignments;
};

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

Cadical::Cadical()
{
  d_solver.reset(new CaDiCaL::Solver());
  d_decision_prop.reset(new CadicalDecisionPropagator());
  d_solver->connect_external_propagator(d_decision_prop.get());
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
  static_cast<CadicalDecisionPropagator*>(d_decision_prop.get())->clear();
}

void
Cadical::add_decision_priority_lit(int32_t lit, uint32_t priority)
{
  assert(lit);
  static_cast<CadicalDecisionPropagator*>(d_decision_prop.get())
      ->add_lit(lit, priority);
  d_solver->add_observed_var(std::abs(lit));
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
