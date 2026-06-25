; Symmetric bit-vector choice with solve-before.
; Different --seed values should be able to choose different A bits.
(set-logic QF_BV)
(set-option :produce-models true)
(set-info :status sat)

(declare-fun A () (_ BitVec 16))
(declare-fun B () (_ BitVec 16))

; For each bit exactly one of A/B is true. This prevents model completion from
; hiding the SAT-level phase choice behind an all-zero don't-care assignment.
(assert (= (bvxor A B) #xffff))
(set-solve-before A B)

(check-sat)
(get-value (A))
(get-value (B))
(exit)
