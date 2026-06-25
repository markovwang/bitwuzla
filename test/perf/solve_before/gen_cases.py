#!/usr/bin/env python3
"""Generate opt-in SMT2 cases for solve-before performance probes."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable


CASE_NAMES = [
    "wide_and_256",
    "wide_and_4096",
    "extra_cone_256",
    "extra_cone_2048",
    "tier_chain_128",
    "duplicate_edges",
]


def bv_zero(width: int) -> str:
    assert width % 4 == 0
    return "#x" + ("0" * (width // 4))


def lines_to_text(lines: Iterable[str]) -> str:
    return "\n".join(lines) + "\n"


def write_case(path: Path, lines: Iterable[str]) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(lines_to_text(lines), encoding="utf-8")
    return path


def wide_and(width: int, with_solve_before: bool) -> list[str]:
    lines = [
        "(set-logic QF_BV)",
        "(set-info :status sat)",
        f"; wide_and_{width}: same wide bit-vector formula with optional priority hints.",
        f"(declare-fun A () (_ BitVec {width}))",
        f"(declare-fun B () (_ BitVec {width}))",
        f"(assert (= (bvand A B) {bv_zero(width)}))",
    ]
    if with_solve_before:
        lines.append("(set-solve-before A B)")
    lines.append("(check-sat)")
    return lines


def extra_cone(width: int, with_solve_before: bool) -> list[str]:
    lines = [
        "(set-logic QF_BV)",
        "(set-info :status sat)",
        f"; extra_cone_{width}: priority term (bvadd A B) is outside assertions.",
        f"(declare-fun A () (_ BitVec {width}))",
        f"(declare-fun B () (_ BitVec {width}))",
        f"(declare-fun C () (_ BitVec {width}))",
        f"(declare-fun D () (_ BitVec {width}))",
        f"(assert (= (bvand C D) {bv_zero(width)}))",
    ]
    if with_solve_before:
        lines.append("(set-solve-before (bvadd A B) C)")
    lines.append("(check-sat)")
    return lines


def tier_chain(size: int, with_solve_before: bool) -> list[str]:
    names = [f"P{i:03d}" for i in range(size)]
    lines = [
        "(set-logic QF_BV)",
        "(set-info :status sat)",
        f"; tier_chain_{size}: many priority buckets over Boolean terms.",
    ]
    lines.extend(f"(declare-fun {name} () Bool)" for name in names)
    lines.append("(assert (or " + " ".join(names) + "))")
    if with_solve_before:
        for lhs, rhs in zip(names, names[1:]):
            lines.append(f"(set-solve-before {lhs} {rhs})")
    lines.append("(check-sat)")
    return lines


def duplicate_edges(with_solve_before: bool) -> list[str]:
    lines = [
        "(set-logic QF_BV)",
        "(set-info :status sat)",
        "; duplicate_edges: repeated graph edges plus overlapping priority bits.",
        "(declare-fun A () (_ BitVec 32))",
        "(declare-fun C () (_ BitVec 32))",
        "(assert (= (bvand A C) #x00000000))",
    ]
    if with_solve_before:
        lines.extend(
            [
                "(set-solve-before A C)",
                "(set-solve-before A C)",
                "(set-solve-before ((_ extract 31 16) A) C)",
                "(set-solve-before ((_ extract 31 16) A) C)",
            ]
        )
    lines.append("(check-sat)")
    return lines


def generate_cases(out_dir: Path) -> list[Path]:
    generated: list[Path] = []
    specs = [
        ("wide_and_256", lambda sb: wide_and(256, sb)),
        ("wide_and_4096", lambda sb: wide_and(4096, sb)),
        ("extra_cone_256", lambda sb: extra_cone(256, sb)),
        ("extra_cone_2048", lambda sb: extra_cone(2048, sb)),
        ("tier_chain_128", lambda sb: tier_chain(128, sb)),
        ("duplicate_edges", duplicate_edges),
    ]
    for name, builder in specs:
        generated.append(write_case(out_dir / f"{name}_base.smt2", builder(False)))
        generated.append(write_case(out_dir / f"{name}_sb.smt2", builder(True)))
    return generated


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out",
        default="test/perf/solve_before/cases",
        help="directory for generated SMT2 files",
    )
    args = parser.parse_args()
    for path in generate_cases(Path(args.out)):
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
