; Boolean symmetric choice with solve-before. Exactly one variable is true.
(set-logic QF_BV)
(set-option :produce-models true)
(set-info :status sat)

(declare-fun P () Bool)
(declare-fun Q () Bool)
(declare-fun R () Bool)
(declare-fun S () Bool)

(assert (or P Q R S))
(assert (or (not P) (not Q)))
(assert (or (not P) (not R)))
(assert (or (not P) (not S)))
(assert (or (not Q) (not R)))
(assert (or (not Q) (not S)))
(assert (or (not R) (not S)))

(set-solve-before P Q)
(set-solve-before Q R)
(set-solve-before R S)

(check-sat)
(get-value (P))
(get-value (Q))
(get-value (R))
(get-value (S))
(exit)
