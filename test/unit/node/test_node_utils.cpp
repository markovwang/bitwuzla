/***
 * Bitwuzla: Satisfiability Modulo Theories (SMT) solver.
 *
 * Copyright (C) 2022 by the authors listed in the AUTHORS file at
 * https://github.com/bitwuzla/bitwuzla/blob/main/AUTHORS
 *
 * This file is part of Bitwuzla under the MIT license. See COPYING for more
 * information at https://github.com/bitwuzla/bitwuzla/blob/main/COPYING
 */

#include <gtest/gtest.h>

#include "env.h"
#include "node/node_manager.h"
#include "node/node_utils.h"
#include "rewrite/rewriter.h"
#include "sat/sat_solver_factory.h"
#include "solver/fp/floating_point.h"
#include "solver/fp/rounding_mode.h"

namespace bzla::test {

using namespace bzla::node;

class TestNodeUtils : public ::testing::Test
{
  void SetUp() override
  {
    d_a        = d_nm.mk_const(d_nm.mk_bool_type());
    d_b        = d_nm.mk_const(d_nm.mk_bool_type());
    d_c        = d_nm.mk_const(d_nm.mk_bool_type());
    d_bv4_type = d_nm.mk_bv_type(4);
    d_a4       = d_nm.mk_const(d_bv4_type);
    d_b4       = d_nm.mk_const(d_bv4_type);
    d_c4       = d_nm.mk_const(d_bv4_type);
  }

 protected:
  TestNodeUtils()
      : d_sat_factory(d_options),
        d_env(d_nm, d_sat_factory),
        d_rewriter(d_env.rewriter())
  {
  }

  NodeManager d_nm;
  option::Options d_options;
  sat::SatSolverFactory d_sat_factory;
  Env d_env;
  Rewriter& d_rewriter;
  Type d_bv4_type;
  Node d_a;
  Node d_b;
  Node d_c;
  Node d_a4;
  Node d_b4;
  Node d_c4;
};

TEST_F(TestNodeUtils, is_bv_sext)
{
  Node res, child;
  RewriteRuleKind kind;
  Node bvsext = d_nm.mk_node(Kind::BV_SIGN_EXTEND, {d_a4}, {3});
  ASSERT_TRUE(utils::is_bv_sext(bvsext, child));
  ASSERT_EQ(child, d_a4);
  std::tie(res, kind) =
      RewriteRule<RewriteRuleKind::BV_SIGN_EXTEND_ELIM>::apply(d_rewriter,
                                                               bvsext);
  assert(utils::is_bv_sext(res, child));
  ASSERT_TRUE(utils::is_bv_sext(res, child));
  ASSERT_EQ(child, d_a4);
  bvsext = d_nm.mk_node(
      Kind::BV_CONCAT,
      {d_nm.mk_node(
           Kind::ITE,
           {d_nm.mk_node(Kind::EQUAL,
                         {d_nm.mk_node(Kind::BV_EXTRACT, {d_a4}, {3, 3}),
                          d_nm.mk_value(BitVector::mk_one(1))}),
            d_nm.mk_value(BitVector::mk_ones(3)),
            d_nm.mk_value(BitVector::mk_zero(3))}),
       d_a4});
  ASSERT_TRUE(utils::is_bv_sext(bvsext, child));
  ASSERT_EQ(child, d_a4);
  bvsext = d_nm.mk_node(
      Kind::BV_CONCAT,
      {d_nm.mk_node(
           Kind::ITE,
           {d_nm.mk_node(Kind::EQUAL,
                         {d_nm.mk_value(BitVector::mk_one(1)),
                          d_nm.mk_node(Kind::BV_EXTRACT, {d_a4}, {3, 3})}),
            d_nm.mk_value(BitVector::mk_ones(3)),
            d_nm.mk_value(BitVector::mk_zero(3))}),
       d_a4});
  ASSERT_TRUE(utils::is_bv_sext(bvsext, child));
  ASSERT_EQ(child, d_a4);
  bvsext = d_nm.mk_node(
      Kind::BV_CONCAT,
      {d_nm.mk_node(
           Kind::ITE,
           {d_nm.mk_node(Kind::EQUAL,
                         {d_nm.mk_node(Kind::BV_EXTRACT, {d_a4}, {3, 3}),
                          d_nm.mk_value(BitVector::mk_zero(1))}),
            d_nm.mk_value(BitVector::mk_zero(3)),
            d_nm.mk_value(BitVector::mk_ones(3))}),
       d_a4});
  ASSERT_TRUE(utils::is_bv_sext(bvsext, child));
  ASSERT_EQ(child, d_a4);
  bvsext = d_nm.mk_node(
      Kind::BV_CONCAT,
      {d_nm.mk_node(
           Kind::ITE,
           {d_nm.mk_node(Kind::EQUAL,
                         {d_nm.mk_value(BitVector::mk_zero(1)),
                          d_nm.mk_node(Kind::BV_EXTRACT, {d_a4}, {3, 3})}),
            d_nm.mk_value(BitVector::mk_zero(3)),
            d_nm.mk_value(BitVector::mk_ones(3))}),
       d_a4});
  ASSERT_TRUE(utils::is_bv_sext(bvsext, child));
  ASSERT_EQ(child, d_a4);
  bvsext = d_nm.mk_node(
      Kind::BV_CONCAT,
      {d_nm.mk_node(
           Kind::ITE,
           {d_nm.mk_node(Kind::EQUAL,
                         {d_nm.mk_value(BitVector::mk_zero(1)),
                          d_nm.mk_node(Kind::BV_EXTRACT, {d_a4}, {3, 3})}),
            d_nm.mk_value(BitVector::mk_ones(3)),
            d_nm.mk_value(BitVector::mk_zero(3))}),
       d_a4});
  ASSERT_FALSE(utils::is_bv_sext(bvsext, child));
  ASSERT_FALSE(utils::is_bv_sext(
      d_nm.mk_node(Kind::BV_ZERO_EXTEND, {d_a4}, {3}), child));
}

TEST_F(TestNodeUtils, next_value)
{
  {
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(false)),
              d_nm.mk_value(true));
    ASSERT_TRUE(utils::next_value(d_nm, d_nm.mk_value(true)).is_null());
  }

  {
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(BitVector::from_ui(2, 0))),
              d_nm.mk_value(BitVector::from_ui(2, 1)));
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(BitVector::from_ui(2, 1))),
              d_nm.mk_value(BitVector::from_ui(2, 2)));
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(BitVector::from_ui(2, 2))),
              d_nm.mk_value(BitVector::from_ui(2, 3)));
    ASSERT_TRUE(utils::next_value(d_nm, d_nm.mk_value(BitVector::from_ui(2, 3)))
                    .is_null());
  }

  {
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(RoundingMode::RNA)),
              d_nm.mk_value(RoundingMode::RNE));
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(RoundingMode::RNE)),
              d_nm.mk_value(RoundingMode::RTN));
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(RoundingMode::RTN)),
              d_nm.mk_value(RoundingMode::RTP));
    ASSERT_EQ(utils::next_value(d_nm, d_nm.mk_value(RoundingMode::RTP)),
              d_nm.mk_value(RoundingMode::RTZ));
    ASSERT_TRUE(
        utils::next_value(d_nm, d_nm.mk_value(RoundingMode::RTZ)).is_null());
  }

  {
    Type fp8     = d_nm.mk_fp_type(3, 5);
    Node n       = d_nm.mk_value(FloatingPoint::fpzero(3, 5, false));
    BitVector bv = BitVector::mk_zero(8);

    while (!n.value<FloatingPoint>().fpisnan())
    {
      ASSERT_EQ(n.value<FloatingPoint>().as_bv(), bv);
      bv.flip_bit(bv.size() - 1);
      if (!bv.msb())
      {
        bv.ibvinc();
      }
      n = utils::next_value(d_nm, n);
    }
    n = utils::next_value(d_nm, n);
    ASSERT_TRUE(n.is_null());
  }
}

}  // namespace bzla::test
