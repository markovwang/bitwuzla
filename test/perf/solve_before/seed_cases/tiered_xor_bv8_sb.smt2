; Three-tier bit-vector choice with solve-before ordering A before B before C.
; The equations leave choices open but force printed values to reflect them.
(set-logic QF_BV)
(set-option :produce-models true)
(set-info :status sat)

(declare-fun A () (_ BitVec 8))
(declare-fun B () (_ BitVec 8))
(declare-fun C () (_ BitVec 8))

(assert (= (bvxor A B) #xff))
(assert (= (bvxor B C) #x00))
(set-solve-before A B)
(set-solve-before B C)

(check-sat)
(get-value (A))
(get-value (B))
(get-value (C))
(exit)
