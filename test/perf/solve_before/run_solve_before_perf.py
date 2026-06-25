#!/usr/bin/env python3
"""Run opt-in solve-before performance probes and collect statistics."""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path
from typing import Any

from gen_cases import CASE_NAMES, generate_cases


STAT_COLUMNS = [
    "solving_context::time_solve",
    "solver::engine::time_solve",
    "solver::bv::bitblast::sat::time_solve",
    "solver::bv::bitblast::aig::num_ands",
    "solver::bv::bitblast::cnf::num_vars",
    "solver::bv::bitblast::cnf::num_clauses",
    "solver::bv::bitblast::cnf::num_literals",
    "solver::bv::bitblast::decision_priority::time_register",
    "solver::bv::bitblast::decision_priority::time_bitblast",
    "solver::bv::bitblast::decision_priority::time_encode",
    "solver::bv::bitblast::decision_priority::register_rounds",
    "solver::bv::bitblast::decision_priority::terms",
    "solver::bv::bitblast::decision_priority::bits",
    "solver::bv::bitblast::decision_priority::const_bits",
    "solver::bv::bitblast::decision_priority::lits",
    "solver::bv::bitblast::decision_priority::sat::add_lit_calls",
    "solver::bv::bitblast::decision_priority::sat::add_lit_duplicates",
    "solver::bv::bitblast::decision_priority::sat::unique_lits",
    "solver::bv::bitblast::decision_priority::sat::max_priority_buckets",
    "solver::bv::bitblast::decision_priority::sat::native_seed",
    "solver::bv::bitblast::decision_priority::sat::native_decide_calls",
    "solver::bv::bitblast::decision_priority::sat::native_decide_returns",
    "solver::bv::bitblast::decision_priority::sat::native_decide_fallbacks",
    "solver::bv::bitblast::decision_priority::sat::native_decide_positive",
    "solver::bv::bitblast::decision_priority::sat::native_decide_negative",
    "solver::bv::bitblast::decision_priority::sat::observed_var_calls",
    "solver::bv::bitblast::decision_priority::sat::cb_decide_calls",
    "solver::bv::bitblast::decision_priority::sat::cb_decide_returns",
    "solver::bv::bitblast::decision_priority::sat::cb_decide_fallbacks",
    "solver::bv::bitblast::decision_priority::sat::cb_decide_scanned_lits",
    "solver::bv::bitblast::decision_priority::sat::cb_decide_max_scan",
    "solver::bv::bitblast::decision_priority::sat::notify_assignment_calls",
    "solver::bv::bitblast::decision_priority::sat::notify_backtrack_calls",
    "solver::bv::bitblast::decision_priority::sat::notify_new_decision_level_calls",
    "solver::bv::bitblast::decision_priority::sat::notify_backtrack_erased_vars",
]

STAT_RE = re.compile(r"^([A-Za-z0-9_:]+):\s*(.*)$")
NUMBER_RE = re.compile(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?")


def case_and_variant(path: Path) -> tuple[str, str]:
    stem = path.stem
    if stem.endswith("_base"):
        return stem[:-5], "base"
    if stem.endswith("_sb"):
        return stem[:-3], "sb"
    raise ValueError(f"unexpected generated case name: {path.name}")


def select_cases(paths: list[Path], filters: list[str]) -> list[Path]:
    if not filters:
        return paths
    selected = []
    wanted = set(filters)
    for path in paths:
        case, variant = case_and_variant(path)
        if case in wanted or path.stem in wanted or f"{case}_{variant}" in wanted:
            selected.append(path)
    return selected


def parse_stats(text: str) -> dict[str, str]:
    stats: dict[str, str] = {}
    for line in text.splitlines():
        match = STAT_RE.match(line.strip())
        if match:
            stats[match.group(1)] = match.group(2).strip()
    return stats


def parse_result(text: str, returncode: int) -> str:
    for line in text.splitlines():
        stripped = line.strip()
        if stripped in {"sat", "unsat", "unknown"}:
            return stripped
    return "error" if returncode else "missing-result"


def numeric_value(value: str | None) -> float | None:
    if not value:
        return None
    match = NUMBER_RE.search(value)
    if not match:
        return None
    try:
        return float(match.group(0))
    except ValueError:
        return None


def median_number(values: list[str]) -> float | None:
    nums = [num for num in (numeric_value(value) for value in values) if num is not None]
    if not nums:
        return None
    return statistics.median(nums)


def run_solver(
    binary: Path,
    case_path: Path,
    mode_args: list[str],
    timeout: float | None,
) -> dict[str, Any]:
    cmd = [
        str(binary),
        "--no-check-model",
        "--verbosity",
        "1",
        *mode_args,
        str(case_path),
    ]
    start = time.perf_counter()
    proc = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    elapsed = time.perf_counter() - start
    combined = proc.stdout + proc.stderr
    case, variant = case_and_variant(case_path)
    return {
        "case": case,
        "variant": variant,
        "path": str(case_path),
        "cmd": cmd,
        "returncode": proc.returncode,
        "result": parse_result(combined, proc.returncode),
        "wall_seconds": elapsed,
        "stats": parse_stats(combined),
        "stdout_tail": proc.stdout.splitlines()[-40:],
        "stderr_tail": proc.stderr.splitlines()[-40:],
    }


def summarize(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[tuple[str, str, str], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        groups[(record["mode"], record["case"], record["variant"])].append(record)

    rows: list[dict[str, Any]] = []
    for (mode, case, variant), group in sorted(groups.items()):
        row: dict[str, Any] = {
            "mode": mode,
            "case": case,
            "variant": variant,
            "runs": len(group),
            "result": ",".join(sorted({str(item["result"]) for item in group})),
            "median_wall_seconds": statistics.median(
                float(item["wall_seconds"]) for item in group
            ),
        }
        for stat in STAT_COLUMNS:
            value = median_number([item["stats"].get(stat, "") for item in group])
            row[stat] = "" if value is None else value
        rows.append(row)
    return rows


def write_json(path: Path, payload: Any) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "mode",
        "case",
        "variant",
        "runs",
        "result",
        "median_wall_seconds",
        *STAT_COLUMNS,
    ]
    with path.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def run_perf(
    binary: Path,
    case_path: Path,
    mode_name: str,
    mode_args: list[str],
    out_dir: Path,
    timeout: float | None,
) -> dict[str, Any]:
    perf_dir = out_dir / "perf"
    perf_dir.mkdir(parents=True, exist_ok=True)
    stem = f"{case_path.stem}.{mode_name}"
    data_path = perf_dir / f"{stem}.perf.data"
    report_path = perf_dir / f"{stem}.perf.txt"
    cmd = [
        str(binary),
        "--no-check-model",
        "--verbosity",
        "1",
        *mode_args,
        str(case_path),
    ]
    record_cmd = ["perf", "record", "-g", "-o", str(data_path), "--", *cmd]
    record = subprocess.run(
        record_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
    )
    report_cmd = [
        "perf",
        "report",
        "--stdio",
        "--percent-limit",
        "1",
        "-i",
        str(data_path),
    ]
    report_stdout = ""
    report_stderr = ""
    report_returncode = None
    if data_path.exists():
        report = subprocess.run(
            report_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
        )
        report_stdout = report.stdout
        report_stderr = report.stderr
        report_returncode = report.returncode
        report_path.write_text(report_stdout + report_stderr, encoding="utf-8")

    return {
        "case": case_path.stem,
        "mode": mode_name,
        "record_cmd": record_cmd,
        "record_returncode": record.returncode,
        "record_stdout_tail": record.stdout.splitlines()[-40:],
        "record_stderr_tail": record.stderr.splitlines()[-40:],
        "report_cmd": report_cmd,
        "report_returncode": report_returncode,
        "report_stdout_tail": report_stdout.splitlines()[:80],
        "report_stderr_tail": report_stderr.splitlines()[-40:],
        "perf_data": str(data_path),
        "perf_report": str(report_path),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="build/src/main/bitwuzla")
    parser.add_argument("--out", default="/tmp/bitwuzla-sb-perf")
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--case", action="append", default=[])
    parser.add_argument(
        "--mode",
        choices=["both", "default", "no-preprocess"],
        default="both",
    )
    parser.add_argument("--timeout", type=float, default=None)
    parser.add_argument("--perf", action="store_true")
    parser.add_argument("--perf-case", action="append", default=[])
    args = parser.parse_args()

    binary = Path(args.binary)
    out_dir = Path(args.out)
    case_dir = out_dir / "cases"
    out_dir.mkdir(parents=True, exist_ok=True)
    generated = generate_cases(case_dir)
    selected = select_cases(generated, args.case)
    if not selected:
        print(f"no cases selected from filters: {args.case}", file=sys.stderr)
        return 2

    modes = []
    if args.mode in {"both", "default"}:
        modes.append(("default", []))
    if args.mode in {"both", "no-preprocess"}:
        modes.append(("no-preprocess", ["--no-preprocess"]))

    records: list[dict[str, Any]] = []
    for mode_name, mode_args in modes:
        for case_path in selected:
            for index in range(args.repeat):
                record = run_solver(binary, case_path, mode_args, args.timeout)
                record["mode"] = mode_name
                record["repeat"] = index
                records.append(record)
                print(
                    f"{mode_name} {case_path.stem} run {index + 1}/{args.repeat}: "
                    f"{record['result']} {record['wall_seconds']:.6f}s"
                )

    summary = summarize(records)
    payload: dict[str, Any] = {
        "binary": str(binary),
        "out_dir": str(out_dir),
        "case_names": CASE_NAMES,
        "records": records,
        "summary": summary,
    }

    perf_results = []
    if args.perf:
        perf_filters = args.perf_case or ["wide_and_4096_sb"]
        perf_cases = select_cases(generated, perf_filters)
        for mode_name, mode_args in modes:
            for case_path in perf_cases:
                print(f"perf {mode_name} {case_path.stem}")
                perf_results.append(
                    run_perf(binary, case_path, mode_name, mode_args, out_dir, args.timeout)
                )
        payload["perf"] = perf_results

    write_json(out_dir / "results.json", payload)
    write_csv(out_dir / "results.csv", summary)
    print(f"wrote {out_dir / 'results.json'}")
    print(f"wrote {out_dir / 'results.csv'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
