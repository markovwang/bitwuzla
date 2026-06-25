; Symmetric bit-vector choice without solve-before, for baseline comparison.
(set-logic QF_BV)
(set-option :produce-models true)
(set-info :status sat)

(declare-fun A () (_ BitVec 16))
(declare-fun B () (_ BitVec 16))

(assert (= (bvxor A B) #xffff))

(check-sat)
(get-value (A))
(get-value (B))
(exit)
