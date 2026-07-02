#!/usr/bin/env python3
import argparse
import datetime as dt
import os
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_TAG = "s05_6_2_fixed_128_mac_74mhz"
LOG_DIR = REPO / "logs"
REPORT_DIR = REPO / "reports"
SUMMARY = None


def append_summary(text: str) -> None:
    if SUMMARY is None:
        raise RuntimeError("SUMMARY is not initialized")
    with SUMMARY.open("a", encoding="ascii") as f:
        f.write(text)
        if not text.endswith("\n"):
            f.write("\n")


def run_step(name: str, cmd: list[str], env_updates: dict[str, str], timeout: int) -> int:
    env = os.environ.copy()
    env.update(env_updates)
    rendered_env = " ".join(f"{k}={v}" for k, v in sorted(env_updates.items()))
    rendered_cmd = " ".join(cmd)
    append_summary(f"\n## {name}")
    append_summary(f"time: {dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    append_summary(f"env: {rendered_env}")
    append_summary(f"cmd: {rendered_cmd}")
    print(f"\n[BOARD_GATE] {name}")
    print(f"env {rendered_env} {rendered_cmd}")
    proc = subprocess.run(cmd, cwd=REPO, env=env, timeout=timeout)
    append_summary(f"rc: {proc.returncode}")
    print(f"[BOARD_GATE] {name} rc={proc.returncode}")
    return proc.returncode


def main() -> int:
    global SUMMARY
    parser = argparse.ArgumentParser(
        description="Run the S05.6.2 board gate in the required order."
    )
    parser.add_argument("--tag", default=os.environ.get("S05_6_2_TAG", DEFAULT_TAG))
    parser.add_argument("--boot-path", default=os.environ.get("S05_6_2_BOOT_PATH", ""))
    parser.add_argument("--boot-sha256", default=os.environ.get("S05_6_2_BOOT_SHA256", ""))
    parser.add_argument("--serial", default=os.environ.get("S05_SERIAL", "/dev/ttyUSB1"))
    parser.add_argument("--baud", default=os.environ.get("S05_BAUD", "115200"))
    parser.add_argument("--skip-batch-proxy", action="store_true",
                        help="Stop after S05.5 fake 100-run even if multi-block passes.")
    args = parser.parse_args()
    tag = args.tag
    SUMMARY = LOG_DIR / f"{tag}_board_gate_summary.txt"

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    SUMMARY.write_text(
        "S05.6.2 board gate summary\n"
        f"created: {dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n"
        f"tag: {tag}\n"
        f"serial: {args.serial}\n"
        f"baud: {args.baud}\n"
        f"expected BOOT: {args.boot_path or 'not provided'}\n"
        f"expected BOOT sha256: {args.boot_sha256 or 'not provided'}\n",
        encoding="ascii",
    )

    common = {
        "S05_SERIAL": args.serial,
        "S05_6_1_SERIAL": args.serial,
        "S05_6_SERIAL": args.serial,
        "S05_BAUD": str(args.baud),
    }

    steps = [
        (
            "1_boot_root_and_s05_fake_one_run",
            ["python3", "scripts/s05_gemv_hw_test.py"],
            {
                **common,
                "S05_LOG_PATH": f"logs/{tag}_s05_fake_one_run.txt",
                "S05_EXTRA_ARGS": "--expect-axis-width 128 --full-debug-status",
                "S05_RUN_TIMEOUT": "180",
            },
            260,
        ),
        (
            "2_B_64x16_P0_mode0_repeat5",
            ["python3", "scripts/s05_6_1_multiblock_board.py"],
            {
                **common,
                "S05_6_1_BOARD_LOG": f"logs/{tag}_mode0_B64P0_repeat5_board.txt",
                "S05_6_1_BOARD_CSV": f"reports/{tag}_mode0_B64P0_repeat5_board.csv",
                "S05_6_1_EXTRA_ARGS": "--only B_64x16_P0 --repeat 5",
                "S05_6_1_RUN_TIMEOUT": "240",
            },
            340,
        ),
        (
            "3_B_64x16_P0_mode1_dump",
            ["python3", "scripts/s05_6_1_multiblock_board.py"],
            {
                **common,
                "S05_6_1_BOARD_LOG": f"logs/{tag}_mode1_B64P0_dump_board.txt",
                "S05_6_1_BOARD_CSV": f"reports/{tag}_mode1_B64P0_dump_board.csv",
                "S05_6_1_EXTRA_ARGS": "--only B_64x16_P0 --mode1-blocks",
                "S05_6_1_RUN_TIMEOUT": "240",
            },
            340,
        ),
        (
            "4_full_S05_6_1_multiblock",
            ["python3", "scripts/s05_6_1_multiblock_board.py"],
            {
                **common,
                "S05_6_1_BOARD_LOG": f"logs/{tag}_full_multiblock_board.txt",
                "S05_6_1_BOARD_CSV": f"reports/{tag}_full_multiblock_board.csv",
                "S05_6_1_EXTRA_ARGS": "",
                "S05_6_1_RUN_TIMEOUT": "420",
            },
            540,
        ),
        (
            "5_S05_5_fake_100_run",
            ["python3", "scripts/s05_gemv_hw_test.py"],
            {
                **common,
                "S05_LOG_PATH": f"logs/{tag}_s05_5_fake_quiet_100.txt",
                "S05_EXTRA_ARGS": "--expect-axis-width 128 --repeat 100 --quiet-pass",
                "S05_RUN_TIMEOUT": "420",
            },
            540,
        ),
    ]

    for name, cmd, env, timeout in steps:
        rc = run_step(name, cmd, env, timeout)
        if rc != 0:
            append_summary(f"\nBOARD_GATE_STOP: {name} failed")
            append_summary("S06_GATE: still blocked")
            return rc

    if args.skip_batch_proxy:
        append_summary("\n6_S05_6_batch_proxy: skipped by --skip-batch-proxy")
        append_summary("BOARD_GATE_PARTIAL_PASS_WITH_BATCH_PROXY_SKIPPED")
        append_summary("S06_GATE: still blocked")
        return 0

    rc = run_step(
        "6_S05_6_batch_proxy_after_multiblock_pass",
        ["python3", "scripts/s05_6_batch_bench.py"],
        {
            **common,
            "S05_6_LOG_PATH": f"logs/{tag}_s05_6_batch_benchmark.txt",
            "S05_6_CSV_PATH": f"logs/{tag}_s05_6_batch_benchmark.csv",
            "S05_6_PROXY_CSV_PATH": f"reports/{tag}_s05_6_proxy_benchmark.csv",
            "S05_6_RUN_TIMEOUT": "900",
        },
        1040,
    )
    if rc != 0:
        append_summary("\nBOARD_GATE_STOP: S05.6 batch/proxy failed after multi-block pass")
        append_summary("S06_GATE: still blocked")
        return rc

    append_summary("\nBOARD_GATE_PASS")
    append_summary("S06_GATE: S05.6.2 gate passed; S05.6.3 DMA length/chunking remains before S06")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.TimeoutExpired as exc:
        append_summary(f"\nBOARD_GATE_TIMEOUT: {' '.join(exc.cmd)}")
        append_summary("S06_GATE: still blocked")
        print(f"timeout: {' '.join(exc.cmd)}", file=sys.stderr)
        raise SystemExit(124)
