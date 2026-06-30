#!/usr/bin/env python3
import re
import sys
import time
from pathlib import Path

import serial


PORT = "/dev/ttyUSB1"
BAUD = 115200
SOURCE_PATH = Path("artifacts/boot_tests/s04_5_dma_carveout_smoke.c")
LOG_PATH = Path("logs/s04_5c_dma_carveout_smoke.txt")
BOARD_SOURCE = "/tmp/s04_5_dma_carveout_smoke.c"
BOARD_BIN = "/tmp/s04_5_dma_carveout_smoke"
PHYS_BASE = "0x3c000000"
SIZE = "0x04000000"
PATTERN_COUNT = "64"

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

    def read_until_prompt(self, timeout=30):
        return self.read_until(lambda b: PROMPT_RE.search(b[-65536:]) is not None, timeout)

    def read_until(self, predicate, timeout=30):
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

    def cmd(self, command, timeout=30):
        marker = f"\n\n### HOST_SEND: {command}\n".encode("ascii", "replace")
        self.log.write(marker)
        self.log.flush()
        self.buf.clear()
        self.cmd_seq += 1
        done = f"__S04_5C_CMD_DONE_{self.cmd_seq}__"
        self.write(command + f"\necho {done}=$?\r")
        done_bytes = done.encode("ascii")
        if not self.read_until(lambda b: done_bytes in b[-65536:], timeout=timeout):
            print(f"\nTIMEOUT waiting for completion marker after: {command}", file=sys.stderr)
            return False
        self.read_until_prompt(timeout=3)
        return True


def main():
    source = SOURCE_PATH.read_text(encoding="ascii")

    s = Session()
    try:
        print(f"Logging to {LOG_PATH}")
        s.write("\r")
        if not s.read_until_prompt(timeout=5):
            print("FAIL: Linux root prompt not detected. Keep the S04.5A mem=960M boot running or reboot into it first.", file=sys.stderr)
            return 2
        s.write("stty -echo\r")
        s.read_until_prompt(timeout=3)
        s.buf.clear()

        checks = (
            "echo __S04_5C_PRECHECK_START__;"
            "cat /proc/cmdline;"
            "echo __IOMEM__;cat /proc/iomem;"
            "echo __TOOLS__;command -v gcc || true;"
            "echo __S04_5C_PRECHECK_END__"
        )
        if not s.cmd(checks, timeout=20):
            return 3

        upload = (
            f"rm -f {BOARD_SOURCE} {BOARD_BIN}; "
            f"cat > {BOARD_SOURCE} <<'__S04_5C_C_SOURCE__'\n"
            f"{source}"
            "__S04_5C_C_SOURCE__\n"
            f"wc -c {BOARD_SOURCE}"
        )
        if not s.cmd(upload, timeout=30):
            return 4

        compile_cmd = (
            f"gcc -O2 -Wall -Wextra -std=c99 -o {BOARD_BIN} {BOARD_SOURCE}; "
            "rc=$?; "
            "echo __S04_5C_COMPILE_RC__=$rc; "
            f"[ $rc -eq 0 ] && ls -l {BOARD_BIN}"
        )
        if not s.cmd(compile_cmd, timeout=60):
            return 5
        if b"__S04_5C_COMPILE_RC__=0" not in LOG_PATH.read_bytes():
            print("FAIL: board compile did not pass", file=sys.stderr)
            return 5

        run_cmd = (
            "echo __S04_5C_RUN_START__;"
            f"{BOARD_BIN} --phys-base {PHYS_BASE} --size {SIZE} --pattern-count {PATTERN_COUNT}; "
            "rc=$?; echo __S04_5C_RUN_RC__=$rc; "
            "echo __S04_5C_DMESG_CHECK__; "
            "dmesg | tail -120 | grep -Ei 'panic|oops|bug:|bus error|external abort|imprecise|segfault|devmem|DMA|uio' || true; "
            "echo __S04_5C_RUN_END__"
        )
        if not s.cmd(run_cmd, timeout=60):
            return 6

        log_bytes = LOG_PATH.read_bytes()
        pass_markers = [
            b"__S04_5C_RUN_RC__=0",
            b"SUMMARY PASS",
            b"__S04_5C_RUN_END__",
            b"00000000-3bffffff : System RAM",
            b"mem=960M",
        ]
        missing = [m.decode("ascii") for m in pass_markers if m not in log_bytes]
        if missing:
            print(f"FAIL: missing expected markers: {', '.join(missing)}", file=sys.stderr)
            return 7

        run_start = log_bytes.rfind(b"__S04_5C_RUN_START__")
        run_end = log_bytes.rfind(b"__S04_5C_RUN_END__")
        if run_start < 0 or run_end < run_start:
            print("FAIL: run block markers are missing or out of order", file=sys.stderr)
            return 8

        upper_tail = log_bytes[run_start:run_end].upper()
        bad_markers = [b"KERNEL PANIC", b"OOPS", b"EXTERNAL ABORT", b"SIGBUS", b"MISMATCH"]
        found_bad = [m.decode("ascii") for m in bad_markers if m in upper_tail]
        if found_bad:
            print(f"FAIL: failure markers present: {', '.join(found_bad)}", file=sys.stderr)
            return 8

        summary = (
            "\n### HOST_VERIFICATION_SUMMARY\n"
            "S04.5C result: PASS\n"
            f"source: {SOURCE_PATH}\n"
            f"board binary: {BOARD_BIN}\n"
            f"phys_base: {PHYS_BASE}\n"
            f"size: {SIZE}\n"
            f"pattern_count: {PATTERN_COUNT}\n"
            "/dev/mem mode: O_RDWR|O_SYNC\n"
            "mmap: PASS\n"
            "pattern write/readback: PASS\n"
            "bus error/kernel oops observed: no\n"
            "S05 physical buffer layout candidate: 0x3c000000-0x3fffffff, 64 MiB\n"
        )
        with LOG_PATH.open("ab") as f:
            f.write(summary.encode("ascii"))
        print(summary)
        s.cmd("stty echo", timeout=5)
        return 0
    finally:
        s.close()


if __name__ == "__main__":
    raise SystemExit(main())
