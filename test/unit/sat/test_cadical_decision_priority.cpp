/***
 * Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
 *
 * Copyright (C) 2026 by the authors listed in the AUTHORS file at
 * https://github.com/bitwuzla/bitwuzla/blob/main/AUTHORS
 *
 * This file is part of Bitwuzla under the MIT license. See COPYING for more
 * information at https://github.com/bitwuzla/bitwuzla/blob/main/COPYING
 */

#include "sat/cadical.h"
#include "test/unit/test.h"

namespace bzla::test {

TEST(TestCadicalDecisionPriority, stats_and_clear)
{
  sat::Cadical solver(123);
  solver.add(1);
  solver.add(2);
  solver.add(0);

  solver.clear_decision_priority();
  solver.add_decision_priority_lit(1, 4);
  solver.add_decision_priority_lit(1, 5);
  solver.add_decision_priority_lit(1, 2);
  solver.add_decision_priority_lit(2, 4);

  auto stats = solver.decision_priority_stats();
  ASSERT_EQ(stats.add_lit_calls, 4);
  ASSERT_EQ(stats.add_lit_duplicates, 1);
  ASSERT_EQ(stats.unique_lits, 2);
  ASSERT_EQ(stats.max_priority_buckets, 2);
  ASSERT_EQ(stats.observed_var_calls, 0);
  ASSERT_EQ(stats.cb_decide_calls, 0);
  ASSERT_EQ(stats.notify_assignment_calls, 0);

  ASSERT_EQ(solver.solve(), Result::SAT);
  stats = solver.decision_priority_stats();
  ASSERT_EQ(stats.observed_var_calls, 0);
  ASSERT_EQ(stats.cb_decide_calls, 0);
  ASSERT_EQ(stats.notify_assignment_calls, 0);

  solver.clear_decision_priority();
  ASSERT_EQ(solver.solve(), Result::SAT);
}

TEST(TestCadicalDecisionPriority, same_seed_is_reproducible)
{
  sat::Cadical a(12345);
  sat::Cadical b(12345);
  for (sat::Cadical* solver : {&a, &b})
  {
    solver->add(1);
    solver->add(2);
    solver->add(0);
    solver->add_decision_priority_lit(1, 0);
    solver->add_decision_priority_lit(2, 0);
  }

  ASSERT_EQ(a.solve(), Result::SAT);
  ASSERT_EQ(b.solve(), Result::SAT);
  ASSERT_EQ(a.value(1), b.value(1));
  ASSERT_EQ(a.value(2), b.value(2));
}

}  // namespace bzla::test
