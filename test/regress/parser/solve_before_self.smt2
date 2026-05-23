(set-logic QF_BV)
(declare-fun A () (_ BitVec 8))
(set-solve-before A A)
(check-sat)
