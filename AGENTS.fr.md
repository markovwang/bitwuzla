# Relais Agent : Solve-Before dans Bitwuzla

Ce fichier s'adresse aux agents de codage, par exemple un nouveau fil Codex,
qui doivent continuer le travail dans ce checkout. Ce n'est pas une politique
upstream de Bitwuzla. Verifier toujours la branche et le diff courants avant de
modifier des fichiers, car ce workspace peut contenir des notes locales ou des
fichiers generes non suivis.

## Portee

Depot : `/export/markovw/solve_before_bzla/bitwuzla`

Zone principale : priorite de decision `solve-before` pour Bitwuzla.

Contexte connu : branche `solve-before-v0`, avec la direction native CaDiCaL
priority introduite par le commit `7e070dd6 Use native CaDiCaL priority for
solve-before`.

## Regles de travail

- Garder les modifications strictement limitees a `solve-before`, sauf demande
  explicite de l'utilisateur.
- Ne pas stage ni re-ecrire les fichiers locaux sans rapport, par exemple
  `compile_commands.json`, les brouillons de traduction de spec, les fichiers
  swap d'editeur ou les sorties perf generees.
- Pour compiler ou reconstruire, utiliser 4 coeurs ou moins. Preferer :

```sh
meson compile -C build -j4
```

- Ne pas utiliser plus de parallelisme dans ce checkout.
- Les probes perf dans `test/perf/solve_before/` sont des diagnostics opt-in,
  pas des regressions par defaut.
- L'acces reseau peut etre limite. Pour comparer a upstream, dire clairement si
  la base est la reference locale `origin/*` ou un remote fraichement fetch.

## Direction de conception actuelle

La direction durable est la priorite de decision native dans CaDiCaL, pas
l'ancien prototype avec external propagator.

Points importants :

- `set-solve-before` est un hint de priorite de decision. Il n'ajoute pas de
  contraintes et ne promet pas une semantique de randomisation de type
  SystemVerilog.
- Bitwuzla calcule les tiers solve-before, bit-blast les termes concernes, puis
  enregistre leurs literaux SAT dans le backend SAT.
- Le backend CaDiCaL appelle les API natives :
  `set_decision_priority_seed`, `clear_decision_priority`,
  `add_decision_priority_lit` et `decision_priority_stats`.
- Les variables prioritaires restent des variables internes CaDiCaL normales.
  L'ensemble de priorite est seulement un index auxiliaire sur ces variables,
  pas un second pool de variables.
- Les variables prioritaires doivent etre freeze tant qu'elles sont enregistrees,
  puis melt dans `clear_decision_priority()`. Sinon, le preprocessing peut les
  eliminer avant que le chemin CDCL ne voie la priorite.
- Lorsqu'une variable prioritaire est assignee, elle est retiree de l'ensemble
  candidat. Lors d'un backtrack/unassign, elle est reenfilee si elle reste
  enregistree et eligible.
- Quand il existe des buckets de priorite native actifs, CaDiCaL doit sauter
  `lucky_phases()` ; sinon lucky SAT peut produire un modele avant que le
  selecteur de priorite native ne s'execute.
- Les anciens compteurs external-propagator, comme `cb_decide_*`, `notify_*` et
  observed-var, doivent rester a zero sur le chemin natif.

## Fichiers principaux

Integration Bitwuzla :

- `src/parser/smt2/parser.cpp`
- `src/solving_context.cpp`
- `src/solver/solver_engine.cpp`
- `src/solver/bv/bv_solver.cpp`
- `src/solver/bv/bv_bitblast_solver.cpp`
- `src/solver/bv/bv_bitblast_solver.h`
- `src/sat/sat_solver.h`
- `src/sat/cadical.cpp`
- `src/sat/cadical.h`
- `src/sat/sat_solver_factory.cpp`

Support CaDiCaL vendorise :

- `subprojects/packagefiles/cadical/src/cadical.hpp`
- `subprojects/packagefiles/cadical/src/solver.cpp`
- `subprojects/packagefiles/cadical/src/internal.hpp`
- `subprojects/packagefiles/cadical/src/decide.cpp`
- `subprojects/packagefiles/cadical/src/lucky.cpp`
- `subprojects/packagefiles/cadical/src/backtrack.cpp`
- `subprojects/packagefiles/cadical/src/propagate.cpp`
- `subprojects/packagefiles/cadical/src/flags.cpp`

Si vous modifiez CaDiCaL vendorise, verifier que la source utilisee par le build
et l'overlay Meson restent coherents.

Tests et probes :

- `test/unit/sat/test_cadical_decision_priority.cpp`
- `test/regress/parser/solve_before*.smt2`
- `test/perf/solve_before/README.md`
- `test/perf/solve_before/run_solve_before_perf.py`
- `test/perf/solve_before/run_solve_before_seed_sweep.py`
- `test/perf/solve_before/seed_cases/`

Document lisible :

- `SOLVE_BEFORE_NATIVE_PRIORITY_PLAN_ZH.md`

## Verification suggeree

Utiliser la verification minimale adaptee au changement. Pour les changements
coeur `solve-before` ou CaDiCaL priority, l'ensemble cible habituel est :

```sh
meson compile -C build -j4
meson test -C build sat_cadical_decision_priority --print-errorlogs
meson test -C build parser_solve_before.smt2 parser_solve_before_chain.smt2 parser_solve_before_cycle.smt2 parser_solve_before_self.smt2 --print-errorlogs
python3 test/perf/solve_before/run_solve_before_seed_sweep.py --binary build/src/main/bitwuzla --seeds 32 --width 16
```

Pour les questions de performance, separer ces couts au lieu de ne rapporter
que le temps total :

- materialisation des termes priority : temps bitblast/encode et deltas CNF ;
- overhead d'enregistrement : termes, bits, literaux, doublons ;
- effets sur la recherche SAT : decisions natives, fallbacks, compteurs phase ;
- fuite de l'ancien chemin external : `cb_decide_*`, `notify_*`, observed-var.

## Pieges courants

- Ne pas remettre external propagator comme backend principal de solve-before,
  sauf demande explicite.
- Ne pas traiter la variation de seed comme un echantillonnage uniforme strict
  des modeles. Le controle vise la reproductibilite meme seed et l'equilibre
  entre seeds.
- Ne pas utiliser des probes faibles comme `A & B = 0`; le modele tout-zero peut
  masquer le comportement de phase. Preferer des cas XOR qui forcent un choix
  par bit.
- Ne pas supposer que les variables prioritaires peuvent etre suivies hors du
  pool normal de CaDiCaL. Elles doivent rester des variables normales pour que
  assignment, propagation, analyse de conflit et backtrack utilisent la
  mecanique CaDiCaL existante.
