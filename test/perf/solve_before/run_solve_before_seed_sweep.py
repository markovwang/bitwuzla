#!/usr/bin/env python3
"""Run an opt-in seed sweep for solve-before model-balance checks."""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
from pathlib import Path
from typing import Any


VALUE_RE = re.compile(r"\(\((A|B)\s+#([xb][0-9A-Fa-f]+)\)\)")


def bv_ones(width: int) -> str:
    assert width % 4 == 0
    return "#x" + ("f" * (width // 4))


def case_text(width: int) -> str:
    return "\n".join(
        [
            "(set-logic QF_BV)",
            "(set-option :produce-models true)",
            "(set-info :status sat)",
            f"(declare-fun A () (_ BitVec {width}))",
            f"(declare-fun B () (_ BitVec {width}))",
            f"(assert (= (bvxor A B) {bv_ones(width)}))",
            "(set-solve-before A B)",
            "(check-sat)",
            "(get-value (A))",
            "(get-value (B))",
            "",
        ]
    )


def parse_bv(value: str) -> int:
    if value.startswith("x"):
        return int(value[1:], 16)
    assert value.startswith("b")
    return int(value[1:], 2)


def parse_values(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for name, value in VALUE_RE.findall(text):
        values[name] = parse_bv(value)
    return values


def run_solver(binary: Path, case_path: Path, seed: int, timeout: float | None) -> dict[str, Any]:
    cmd = [str(binary), "--seed", str(seed), str(case_path)]
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    combined = proc.stdout + proc.stderr
    values = parse_values(combined)
    return {
        "seed": seed,
        "cmd": cmd,
        "returncode": proc.returncode,
        "values": values,
        "stdout_tail": proc.stdout.splitlines()[-20:],
        "stderr_tail": proc.stderr.splitlines()[-20:],
    }


def bit_ones(values: list[int], width: int) -> int:
    mask = (1 << width) - 1
    return sum((value & mask).bit_count() for value in values)


def write_csv(path: Path, records: list[dict[str, Any]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=["seed", "A", "B", "returncode"])
        writer.writeheader()
        for record in records:
            values = record["values"]
            writer.writerow(
                {
                    "seed": record["seed"],
                    "A": values.get("A", ""),
                    "B": values.get("B", ""),
                    "returncode": record["returncode"],
                }
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="build/src/main/bitwuzla")
    parser.add_argument("--out", default="/tmp/bitwuzla-sb-seed-sweep")
    parser.add_argument("--seeds", type=int, default=64)
    parser.add_argument("--width", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=None)
    args = parser.parse_args()

    binary = Path(args.binary)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    case_path = out_dir / "solve_before_seed_sweep.smt2"
    case_path.write_text(case_text(args.width), encoding="utf-8")

    records = [
        run_solver(binary, case_path, seed, args.timeout)
        for seed in range(1, args.seeds + 1)
    ]
    repeat = run_solver(binary, case_path, 1, args.timeout)
    reproducible = records[0]["values"] == repeat["values"]

    a_values = [record["values"].get("A", 0) for record in records]
    b_values = [record["values"].get("B", 0) for record in records]
    bit_count = args.seeds * args.width
    summary = {
        "binary": str(binary),
        "case": str(case_path),
        "seeds": args.seeds,
        "width": args.width,
        "same_seed_reproducible": reproducible,
        "unique_a_models": len(set(a_values)),
        "unique_b_models": len(set(b_values)),
        "a_one_fraction": bit_ones(a_values, args.width) / bit_count,
        "b_one_fraction": bit_ones(b_values, args.width) / bit_count,
    }
    payload = {"summary": summary, "records": records, "repeat_seed_1": repeat}
    (out_dir / "results.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8"
    )
    write_csv(out_dir / "results.csv", records)
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
