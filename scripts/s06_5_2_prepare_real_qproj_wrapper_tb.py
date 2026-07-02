#!/usr/bin/env python3
"""Prepare S06.5.2 real q_proj wrapper-TB fixtures without running Vivado."""

from __future__ import annotations

import csv
import hashlib
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "runtime_c" / "smollm2_chat.c"
BUILD_DIR = ROOT / "runtime_c" / "build"
BIN = BUILD_DIR / "smollm2_chat.s06_5_2_fixture"
MODEL = ROOT / "quantized_model" / "original_gguf" / "SmolLM2-135M-Instruct-Q8_0.gguf"
FIXTURE_DIR = ROOT / "golden" / "s06_5_2_real_qproj_wrapper_tb"
LOG_PATH = ROOT / "logs" / "s06_5_2_real_qproj_wrapper_tb.txt"
REPORT_PATH = ROOT / "reports" / "s06_5_2_real_qproj_wrapper_tb.csv"

IDENTITY_CSV = ROOT / "reports" / "s06_5_1_identity_scale_mixed_row.csv"
PACKET_LOG = ROOT / "logs" / "s06_5_1_packet_equivalence_layer0_qproj.txt"
MODE1_LOG = ROOT / "logs" / "s06_5_1_mode1_blockacc_compare.txt"
ONEHOT_LOG = ROOT / "logs" / "s06_5_1_onehot_input_localization.txt"
TB_SOURCE = ROOT / "vivado_ip" / "tb" / "tb_gemv_q8_0_dma_top_multiblock.sv"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def grep_result(path: Path, prefix: str) -> str:
    if not path.exists():
        return "missing"
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix):
            return line.split(":", 1)[-1].strip()
    return "missing"


def read_csv_by_key(path: Path, key_fields: tuple[str, ...]) -> dict[tuple[str, ...], dict[str, str]]:
    if not path.exists():
        return {}
    with path.open(newline="", encoding="utf-8") as f:
        rows = csv.DictReader(f)
        return {tuple(row[k] for k in key_fields): row for row in rows}


def main() -> int:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    FIXTURE_DIR.mkdir(parents=True, exist_ok=True)

    compile_cmd = [
        "gcc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-O2",
        str(SOURCE),
        "-lm",
        "-o",
        str(BIN),
    ]
    compile_res = run(compile_cmd)

    dump_cmd = [
        str(BIN),
        "--model",
        str(MODEL),
        "--prompt-raw",
        "Hi",
        "--act-shift",
        "8",
        "--dump-real-qproj-wrapper-fixture",
        str(FIXTURE_DIR),
    ]
    dump_res = None
    if compile_res.returncode == 0:
        dump_res = run(dump_cmd)

    packet_result = grep_result(PACKET_LOG, "packet_equivalence_result")
    mode1_result = grep_result(MODE1_LOG, "mode1_blockacc_result")
    onehot_result = grep_result(ONEHOT_LOG, "onehot_result")

    identity_rows = read_csv_by_key(IDENTITY_CSV, ("row_group", "lane", "row"))
    affected_rows = read_csv_by_key(FIXTURE_DIR / "affected_rows.csv", ("row_group", "lane", "row"))

    report_rows: list[dict[str, str]] = []
    for key, fixture_row in sorted(
        affected_rows.items(),
        key=lambda kv: (int(kv[0][0]), int(kv[0][1]), int(kv[0][2])),
    ):
        board = identity_rows.get(key, {})
        report_rows.append(
            {
                "row_group": key[0],
                "lane": key[1],
                "row": key[2],
                "fixture_case": fixture_row.get("fixture_case", ""),
                "identity_expected_i32": fixture_row.get("identity_expected_i32", ""),
                "board_mixed_i32": board.get("mixed_i32", ""),
                "board_duplicate_same_lane_i32": board.get("duplicate_same_lane_i32", ""),
                "board_duplicate_lane0_i32": board.get("duplicate_lane0_i32", ""),
                "mixed_vs_cpu": board.get("mixed_vs_cpu", ""),
                "duplicate_same_lane_vs_cpu": board.get("duplicate_same_lane_vs_cpu", ""),
                "mixed_vs_duplicate": board.get("mixed_vs_duplicate", ""),
                "duplicate_good_lanes": board.get("duplicate_good_lanes", ""),
                "packet_equivalence": packet_result,
                "mode1_blockacc": mode1_result,
                "onehot_mode0": onehot_result,
                "simulation_status": "PREPARED_NOT_SIMULATED",
                "fixture_dir": str(FIXTURE_DIR / fixture_row.get("fixture_case", "")),
            }
        )

    fieldnames = [
        "row_group",
        "lane",
        "row",
        "fixture_case",
        "identity_expected_i32",
        "board_mixed_i32",
        "board_duplicate_same_lane_i32",
        "board_duplicate_lane0_i32",
        "mixed_vs_cpu",
        "duplicate_same_lane_vs_cpu",
        "mixed_vs_duplicate",
        "duplicate_good_lanes",
        "packet_equivalence",
        "mode1_blockacc",
        "onehot_mode0",
        "simulation_status",
        "fixture_dir",
    ]
    with REPORT_PATH.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(report_rows)

    case_summary = FIXTURE_DIR / "case_summary.csv"
    case_count = 0
    if case_summary.exists():
        with case_summary.open(newline="", encoding="utf-8") as f:
            case_count = sum(1 for _ in csv.DictReader(f))

    log_lines = [
        "S06.5.2 real q_proj wrapper TB preparation",
        f"status: {'PASS' if compile_res.returncode == 0 and dump_res and dump_res.returncode == 0 else 'FAIL'}",
        "simulation_status: PREPARED_NOT_SIMULATED",
        "vivado_build: not_run",
        "bitstream_boot_xsa_modified: no",
        f"runtime_source: {SOURCE}",
        f"runtime_source_sha256: {sha256(SOURCE)}",
        f"tb_source: {TB_SOURCE}",
        f"tb_source_sha256: {sha256(TB_SOURCE)}",
        f"fixture_dir: {FIXTURE_DIR}",
        f"fixture_case_count: {case_count}",
        f"affected_rows: {len(report_rows)}",
        f"report_csv: {REPORT_PATH}",
        f"packet_equivalence_result: {packet_result}",
        f"mode1_blockacc_result: {mode1_result}",
        f"onehot_result: {onehot_result}",
        "root_rtl_suspicion: ST_SCALE_ACCUM/ST_SCALE_SAT/ST_AFTER_SCALE/ST_EMIT_ROW row_acc emit_lane pipeline",
        "",
        "compile_command: " + " ".join(compile_cmd),
        f"compile_returncode: {compile_res.returncode}",
        compile_res.stdout,
    ]
    if dump_res is not None:
        log_lines.extend(
            [
                "",
                "dump_command: " + " ".join(dump_cmd),
                f"dump_returncode: {dump_res.returncode}",
                dump_res.stdout,
            ]
        )
    LOG_PATH.write_text("\n".join(log_lines), encoding="utf-8")

    return 0 if compile_res.returncode == 0 and dump_res and dump_res.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
