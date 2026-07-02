#!/usr/bin/env python3
import os
import re
import sys
import time
from pathlib import Path

import serial


PORT = os.environ.get("S05_SERIAL", os.environ.get("S05_6_SERIAL", "/dev/ttyUSB1"))
BAUD = int(os.environ.get("S05_BAUD", "115200"))
SOURCE_PATH = Path("runtime_c/gemv_batch_bench.c")
LOG_PATH = Path(os.environ.get("S05_6_LOG_PATH", "logs/s05_6_batch_benchmark.txt"))
CSV_PATH = Path(os.environ.get("S05_6_CSV_PATH", "logs/s05_6_batch_benchmark.csv"))
PROXY_CSV_PATH = Path(os.environ.get("S05_6_PROXY_CSV_PATH", "reports/s05_6_proxy_benchmark.csv"))
BOARD_SOURCE = "/tmp/s05_6_gemv_batch_bench.c"
BOARD_BIN = "/tmp/s05_6_gemv_batch_bench"
PHYS_BASE = os.environ.get("S05_PHYS_BASE", "0x3c000000")
PHYS_SIZE = os.environ.get("S05_PHYS_SIZE", "0x04000000")
EXTRA_ARGS = os.environ.get("S05_6_EXTRA_ARGS", "").strip()
RUN_TIMEOUT = int(os.environ.get("S05_6_RUN_TIMEOUT", "900"))
PROMPT_RE = re.compile(rb"root@Zybo-Z7-20:~#")


class Session:
    def __init__(self):
        LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        self.log = LOG_PATH.open("wb")
        self.ser = serial.Serial(PORT, BAUD, timeout=0.05)
        self.buf = bytearray()
        self.cmd_seq = 0

    def close(self):
        self.log.flush()
        self.log.close()
        self.ser.close()

    def write(self, text):
        data = text.encode("ascii") if isinstance(text, str) else text
        self.ser.write(data)
        self.ser.flush()

    def read_until(self, predicate, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            data = self.ser.read(4096)
            if data:
                self.log.write(data)
                self.log.flush()
                self.buf.extend(data)
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                if predicate(bytes(self.buf)):
                    return True
            else:
                time.sleep(0.02)
        return False

    def read_until_prompt(self, timeout=30):
        return self.read_until(lambda b: PROMPT_RE.search(b[-65536:]) is not None, timeout)

    def cmd(self, command, timeout=30):
        marker = f"\n\n### HOST_SEND: {command[:240]}\n".encode("ascii", "replace")
        self.log.write(marker)
        self.log.flush()
        self.buf.clear()
        self.cmd_seq += 1
        done = f"__S05_6_CMD_DONE_{self.cmd_seq}__"
        self.write(command + f"\necho {done}=$?\r")
        if not self.read_until(lambda b: done.encode("ascii") in b[-65536:], timeout):
            print(f"\nTIMEOUT after command: {command[:160]}", file=sys.stderr)
            return False
        self.read_until_prompt(timeout=3)
        return True


def extract_outputs():
    log = LOG_PATH.read_text(encoding="ascii", errors="replace").splitlines()
    csv_lines = [
        "variant,mode,batch_size,total_us,avg_per_job_us,min_us,p50_us,p95_us,max_us,fail_count,dma_reset_us,config_us,input_bram_write_us,packet_memcpy_us,s2mm_clear_us,dma_setup_us,wait_poll_us"
    ]
    proxy_lines = [
        "name,mode,in_features,out_features,tensor_repeats,row_groups,chunk_count,chunk_in_features,chunk_jobs_per_tensor,total_us,avg_per_tensor_us,avg_per_chunk_us,full_packet_bytes,actual_packet_bytes,actual_result_bytes,full_result_bytes,fail_count,effective_MBps,effective_MMACps,overhead_pct,dma_limit_bytes,cpu_accumulation_required"
    ]
    for line in log:
        if line.startswith("CSV,"):
            csv_lines.append(line.split(",", 1)[1])
        elif line.startswith("PROXYCSV,"):
            proxy_lines.append(line.split(",", 1)[1])
    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    PROXY_CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    CSV_PATH.write_text("\n".join(csv_lines) + "\n", encoding="ascii")
    PROXY_CSV_PATH.write_text("\n".join(proxy_lines) + "\n", encoding="ascii")


def main():
    if not SOURCE_PATH.is_file():
        print(f"missing source: {SOURCE_PATH}", file=sys.stderr)
        return 2
    source = SOURCE_PATH.read_text(encoding="ascii")

    s = Session()
    try:
        print(f"Logging to {LOG_PATH}")
        s.write("\r")
        if not s.read_until_prompt(timeout=10):
            print("FAIL: Linux root prompt not detected. Boot the S05.5 128-bit SD image first.", file=sys.stderr)
            return 2
        s.write("stty -echo\r")
        s.read_until_prompt(timeout=3)
        s.buf.clear()

        precheck = (
            "echo __S05_6_PRECHECK_START__;"
            "cat /proc/cmdline;"
            "echo __UIO__;"
            "for d in /sys/class/uio/uio*; do "
            "echo UIO=$(basename \"$d\") name=$(cat \"$d/name\" 2>/dev/null); "
            "for m in \"$d\"/maps/map*; do [ -e \"$m\" ] && echo map=$(basename \"$m\") addr=$(cat \"$m/addr\") size=$(cat \"$m/size\"); done; "
            "done;"
            "echo __TOOLS__; command -v gcc;"
            "echo __S05_6_PRECHECK_END__"
        )
        if not s.cmd(precheck, timeout=30):
            return 3
        log = LOG_PATH.read_bytes()
        required = [b"mem=960M", b"name=axi_dma", b"name=input_bram", b"name=gemv_ctrl", b"gcc"]
        missing = [x.decode("ascii") for x in required if x not in log]
        if missing:
            print("FAIL: precheck missing " + ", ".join(missing), file=sys.stderr)
            return 3

        if not s.cmd(f"rm -f {BOARD_SOURCE} {BOARD_BIN}", timeout=10):
            return 4
        upload = (
            f"cat > {BOARD_SOURCE} <<'__S05_6_C_SOURCE__'\n"
            f"{source}"
            "__S05_6_C_SOURCE__\n"
            f"wc -c {BOARD_SOURCE}"
        )
        if not s.cmd(upload, timeout=60):
            return 4
        compile_cmd = (
            f"gcc -O2 -Wall -Wextra -std=c99 -o {BOARD_BIN} {BOARD_SOURCE}; "
            "rc=$?; echo __S05_6_COMPILE_RC__=$rc; "
            f"[ $rc -eq 0 ] && ls -l {BOARD_BIN}"
        )
        if not s.cmd(compile_cmd, timeout=120):
            return 5
        if b"__S05_6_COMPILE_RC__=0" not in LOG_PATH.read_bytes():
            print("FAIL: board compile failed", file=sys.stderr)
            return 5

        extra = f" {EXTRA_ARGS}" if EXTRA_ARGS else ""
        run_cmd = (
            "echo __S05_6_RUN_START__;"
            f"{BOARD_BIN} --phys-base {PHYS_BASE} --phys-size {PHYS_SIZE}{extra}; "
            "rc=$?; echo __S05_6_RUN_RC__=$rc; "
            "echo __S05_6_DMESG_CHECK__;"
            "dmesg | tail -180 | grep -Ei 'panic|oops|bug:|bus error|external abort|imprecise|segfault|DMA|uio|gemv|xilinx' || true;"
            "echo __S05_6_RUN_END__"
        )
        if not s.cmd(run_cmd, timeout=RUN_TIMEOUT):
            return 6

        data = LOG_PATH.read_bytes()
        extract_outputs()
        checks = [
            b"__S05_6_RUN_RC__=0",
            b"[S05.6 GEMV BATCH BENCH]",
            b"fake_gemv mode=0 batch PASS",
            b"fake_gemv mode=1 batch PASS",
            b"proxy benchmark PASS",
            b"OVERALL PASS",
        ]
        missing = [x.decode("ascii") for x in checks if x not in data]
        if missing:
            print("FAIL: run missing " + ", ".join(missing), file=sys.stderr)
            print(f"Wrote {CSV_PATH}")
            print(f"Wrote {PROXY_CSV_PATH}")
            return 7
        marker_scan = b"\n".join(
            line for line in data.splitlines() if not line.startswith(b"### HOST_SEND:")
        )
        upper = marker_scan.upper()
        for bad in [b"KERNEL PANIC", b"\nOOPS", b"EXTERNAL ABORT", b"BUS ERROR", b"TIMEOUT WAITING"]:
            if bad in upper:
                print(f"FAIL: bad marker present: {bad.decode('ascii', 'ignore')}", file=sys.stderr)
                return 8

        print(f"Wrote {CSV_PATH}")
        print(f"Wrote {PROXY_CSV_PATH}")
        return 0
    finally:
        try:
            s.cmd("stty echo", timeout=5)
        except Exception:
            pass
        s.close()


if __name__ == "__main__":
    sys.exit(main())
