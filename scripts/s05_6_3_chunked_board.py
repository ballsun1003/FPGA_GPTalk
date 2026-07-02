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
LOG_PATH = Path(os.environ.get("S05_6_3_CHUNK_LOG", "logs/s05_6_3_F_1536x16_chunked_board.txt"))
CSV_PATH = Path(os.environ.get("S05_6_3_CHUNK_CSV", "reports/s05_6_3_F_1536x16_chunked_board.csv"))
BOARD_SOURCE = "/tmp/s05_6_3_gemv_chunked_board.c"
BOARD_BIN = "/tmp/s05_6_3_gemv_chunked_board"
PHYS_BASE = os.environ.get("S05_PHYS_BASE", "0x3c000000")
PHYS_SIZE = os.environ.get("S05_PHYS_SIZE", "0x04000000")
RUN_TIMEOUT = int(os.environ.get("S05_6_3_CHUNK_TIMEOUT", "300"))
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
        done = f"__S05_6_3_CHUNK_CMD_DONE_{self.cmd_seq}__"
        self.write(command + f"\necho {done}=$?\r")
        if not self.read_until(lambda b: done.encode("ascii") in b[-65536:], timeout):
            print(f"\nTIMEOUT after command: {command[:160]}", file=sys.stderr)
            return False
        self.read_until_prompt(timeout=3)
        return True


def extract_csv():
    lines = [
        "name,pattern,mode,in_features,out_features,row_group,chunk_count,chunk_in_features,chunk_packet_bytes,result,classification,first_mismatch,got,expected,elapsed_us,mm2s_sr,s2mm_sr,status,max_btt_ok,cpu_accumulation_required"
    ]
    for line in LOG_PATH.read_text(encoding="ascii", errors="replace").splitlines():
        if line.startswith("CHUNKBOARDCSV,"):
            lines.append(line.split(",", 1)[1])
    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    CSV_PATH.write_text("\n".join(lines) + "\n", encoding="ascii")


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
            print("FAIL: Linux root prompt not detected", file=sys.stderr)
            return 2
        s.write("stty -echo\r")
        s.read_until_prompt(timeout=3)
        s.buf.clear()

        precheck = (
            "echo __S05_6_3_CHUNK_PRECHECK_START__;"
            "cat /proc/cmdline;"
            "for d in /sys/class/uio/uio*; do echo UIO=$(basename \"$d\") name=$(cat \"$d/name\" 2>/dev/null); done;"
            "command -v gcc;"
            "echo __S05_6_3_CHUNK_PRECHECK_END__"
        )
        if not s.cmd(precheck, timeout=30):
            return 3
        data = LOG_PATH.read_bytes()
        for required in [b"mem=960M", b"name=axi_dma", b"name=input_bram", b"name=gemv_ctrl", b"gcc"]:
            if required not in data:
                print(f"FAIL: precheck missing {required.decode()}", file=sys.stderr)
                return 3

        if not s.cmd(f"rm -f {BOARD_SOURCE} {BOARD_BIN}", timeout=10):
            return 4
        upload = (
            f"cat > {BOARD_SOURCE} <<'__S05_6_3_CHUNK_C_SOURCE__'\n"
            f"{source}"
            "__S05_6_3_CHUNK_C_SOURCE__\n"
            f"wc -c {BOARD_SOURCE}"
        )
        if not s.cmd(upload, timeout=60):
            return 4
        compile_cmd = (
            f"gcc -O2 -Wall -Wextra -std=c99 -o {BOARD_BIN} {BOARD_SOURCE}; "
            "rc=$?; echo __S05_6_3_CHUNK_COMPILE_RC__=$rc; "
            f"[ $rc -eq 0 ] && ls -l {BOARD_BIN}"
        )
        if not s.cmd(compile_cmd, timeout=120):
            return 5
        if b"__S05_6_3_CHUNK_COMPILE_RC__=0" not in LOG_PATH.read_bytes():
            print("FAIL: board compile failed", file=sys.stderr)
            return 5

        run_cmd = (
            "echo __S05_6_3_CHUNK_RUN_START__;"
            f"{BOARD_BIN} --phys-base {PHYS_BASE} --phys-size {PHYS_SIZE} --chunked-board-only; "
            "rc=$?; echo __S05_6_3_CHUNK_RUN_RC__=$rc; "
            "echo __S05_6_3_CHUNK_DMESG_CHECK__;"
            "dmesg | tail -180 | grep -Ei 'panic|oops|bug:|bus error|external abort|imprecise|segfault|DMA|uio|gemv|xilinx' || true;"
            "echo __S05_6_3_CHUNK_RUN_END__"
        )
        if not s.cmd(run_cmd, timeout=RUN_TIMEOUT):
            return 6
        extract_csv()
        data = LOG_PATH.read_bytes()
        if b"S05_6_3_CHUNKED_BOARD_PASS" not in data or b"__S05_6_3_CHUNK_RUN_RC__=0" not in data:
            print(f"Wrote {CSV_PATH}")
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
        return 0
    finally:
        try:
            s.cmd("stty echo", timeout=5)
        except Exception:
            pass
        s.close()


if __name__ == "__main__":
    sys.exit(main())
