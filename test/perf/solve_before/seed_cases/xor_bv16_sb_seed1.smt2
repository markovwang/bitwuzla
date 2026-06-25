; Same as xor_bv16_sb.smt2, but self-contained with random-seed 1.
(set-logic QF_BV)
(set-option :produce-models true)
(set-option :random-seed 1)
(set-info :status sat)

(declare-fun A () (_ BitVec 16))
(declare-fun B () (_ BitVec 16))

(assert (= (bvxor A B) #xffff))
(set-solve-before A B)

(check-sat)
(get-value (A))
(get-value (B))
(exit)
