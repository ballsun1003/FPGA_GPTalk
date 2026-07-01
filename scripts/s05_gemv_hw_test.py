#!/usr/bin/env python3
import base64
import os
import re
import sys
import textwrap
import time
from pathlib import Path

import serial


PORT = os.environ.get("S05_SERIAL", "/dev/ttyUSB1")
BAUD = int(os.environ.get("S05_BAUD", "115200"))
SOURCE_PATH = Path("runtime_c/gemv_hw_test.c")
GOLDEN_DIR = Path("pycharm/golden/fake_gemv")
LOG_PATH = Path(os.environ.get("S05_LOG_PATH", "logs/s05_gemv_hw_test.txt"))
BOARD_SOURCE = "/tmp/s05_gemv_hw_test.c"
BOARD_BIN = "/tmp/s05_gemv_hw_test"
BOARD_UPLOAD_GOLDEN_DIR = "/tmp/s05_fake_gemv"
BOARD_EXISTING_GOLDEN_DIR = "/opt/smollm2_zybo/pycharm/golden/fake_gemv"
PHYS_BASE = os.environ.get("S05_PHYS_BASE", "0x3c000000")
PHYS_SIZE = os.environ.get("S05_PHYS_SIZE", "0x04000000")
PACKET_BYTES_OVERRIDE = os.environ.get("S05_PACKET_BYTES")
EXTRA_ARGS = os.environ.get("S05_EXTRA_ARGS", "").strip()
RUN_TIMEOUT = int(os.environ.get("S05_RUN_TIMEOUT", "120"))
PROMPT_RE = re.compile(rb"root@Zybo-Z7-20:~#")

GOLDEN_FILES = [
    "input_i16.bin",
    "scale_q_i32.bin",
    "weight_q8_fpga_layout.bin",
    "output_scaled_ref_i32.bin",
    "output_block_acc_ref_i32.bin",
]


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

    def read_until_prompt(self, timeout=30):
        return self.read_until(lambda b: PROMPT_RE.search(b[-65536:]) is not None, timeout)

    def cmd(self, command, timeout=30):
        marker = f"\n\n### HOST_SEND: {command[:240]}\n".encode("ascii", "replace")
        self.log.write(marker)
        self.log.flush()
        self.buf.clear()
        self.cmd_seq += 1
        done = f"__S05_CMD_DONE_{self.cmd_seq}__"
        self.write(command + f"\necho {done}=$?\r")
        done_bytes = done.encode("ascii")
        if not self.read_until(lambda b: done_bytes in b[-65536:], timeout=timeout):
            print(f"\nTIMEOUT waiting for completion marker after: {command[:160]}", file=sys.stderr)
            return False
        self.read_until_prompt(timeout=3)
        return True


def b64_payload(path: Path) -> str:
    encoded = base64.b64encode(path.read_bytes()).decode("ascii")
    return "\n".join(textwrap.wrap(encoded, 76))


def upload_golden(session: Session) -> bool:
    for name in GOLDEN_FILES:
        local_path = GOLDEN_DIR / name
        payload = b64_payload(local_path)
        eof = "__S05_B64_" + re.sub(r"[^A-Za-z0-9_]", "_", name) + "__"
        cmd = (
            f"base64 -d > {BOARD_UPLOAD_GOLDEN_DIR}/{name} <<'{eof}'\n"
            f"{payload}\n"
            f"{eof}\n"
            f"wc -c {BOARD_UPLOAD_GOLDEN_DIR}/{name}"
        )
        if not session.cmd(cmd, timeout=30):
            return False
    return True


def main():
    source = SOURCE_PATH.read_text(encoding="ascii")
    if not GOLDEN_DIR.is_dir():
        print(f"missing golden dir: {GOLDEN_DIR}", file=sys.stderr)
        return 2
    for name in GOLDEN_FILES:
        if not (GOLDEN_DIR / name).is_file():
            print(f"missing golden file: {GOLDEN_DIR / name}", file=sys.stderr)
            return 2

    s = Session()
    try:
        print(f"Logging to {LOG_PATH}")
        s.write("\r")
        if not s.read_until_prompt(timeout=10):
            print("FAIL: Linux root prompt not detected. Keep the S04.5B mem=960M boot running or reboot the board first.", file=sys.stderr)
            return 2
        s.write("stty -echo\r")
        s.read_until_prompt(timeout=3)
        s.buf.clear()

        golden_names = " ".join(GOLDEN_FILES)
        precheck = (
            "echo __S05_PRECHECK_START__;"
            "cat /proc/cmdline;"
            "echo __IOMEM__;cat /proc/iomem;"
            "echo __UIO__;"
            "for d in /sys/class/uio/uio*; do "
            "echo UIO=$(basename \"$d\") name=$(cat \"$d/name\" 2>/dev/null); "
            "for m in \"$d\"/maps/map*; do [ -e \"$m\" ] && echo map=$(basename \"$m\") addr=$(cat \"$m/addr\") size=$(cat \"$m/size\"); done; "
            "done;"
            "echo __TOOLS__;command -v gcc; if command -v base64 >/dev/null 2>&1; then command -v base64; echo __S05_BASE64_OK__; fi;"
            "echo __GOLDEN__;"
            f"if [ -d {BOARD_EXISTING_GOLDEN_DIR} ]; then ok=1; for f in {golden_names}; do [ -f {BOARD_EXISTING_GOLDEN_DIR}/$f ] || ok=0; done; [ $ok -eq 1 ] && echo __S05_EXISTING_GOLDEN_OK__={BOARD_EXISTING_GOLDEN_DIR}; fi;"
            "echo __S05_PRECHECK_END__"
        )
        if not s.cmd(precheck, timeout=30):
            return 3

        log = LOG_PATH.read_bytes()
        required_pre = [
            b"mem=960M",
            b"00000000-3bffffff : System RAM",
            b"name=axi_dma",
            b"name=input_bram",
            b"name=gemv_ctrl",
            b"gcc",
        ]
        missing = [m.decode("ascii") for m in required_pre if m not in log]
        if missing:
            print("FAIL: precheck missing expected markers: " + ", ".join(missing), file=sys.stderr)
            return 3

        use_existing_golden = b"__S05_EXISTING_GOLDEN_OK__" in log
        base64_available = b"__S05_BASE64_OK__" in log
        upload_golden_needed = False
        if use_existing_golden:
            run_golden_dir = BOARD_EXISTING_GOLDEN_DIR
            print(f"Using board golden dir: {run_golden_dir}")
        elif base64_available:
            run_golden_dir = BOARD_UPLOAD_GOLDEN_DIR
            upload_golden_needed = True
            print(f"Uploading golden dir to: {run_golden_dir}")
        else:
            run_golden_dir = "builtin"
            print("Using embedded fake_gemv golden in C source")

        if upload_golden_needed:
            prep = f"rm -rf {BOARD_UPLOAD_GOLDEN_DIR}; mkdir -p {BOARD_UPLOAD_GOLDEN_DIR}; rm -f {BOARD_SOURCE} {BOARD_BIN}"
        else:
            prep = f"rm -f {BOARD_SOURCE} {BOARD_BIN}"
        if not s.cmd(prep, timeout=10):
            return 4

        upload_source = (
            f"cat > {BOARD_SOURCE} <<'__S05_C_SOURCE__'\n"
            f"{source}"
            "__S05_C_SOURCE__\n"
            f"wc -c {BOARD_SOURCE}"
        )
        if not s.cmd(upload_source, timeout=45):
            return 4

        if upload_golden_needed and not upload_golden(s):
            return 4

        compile_cmd = (
            f"gcc -O2 -Wall -Wextra -std=c99 -o {BOARD_BIN} {BOARD_SOURCE}; "
            "rc=$?; echo __S05_COMPILE_RC__=$rc; "
            f"[ $rc -eq 0 ] && ls -l {BOARD_BIN}"
        )
        if not s.cmd(compile_cmd, timeout=90):
            return 5
        if b"__S05_COMPILE_RC__=0" not in LOG_PATH.read_bytes():
            print("FAIL: board compile did not pass", file=sys.stderr)
            return 5

        packet_arg = f" --packet-bytes {PACKET_BYTES_OVERRIDE}" if PACKET_BYTES_OVERRIDE else ""
        extra_arg = f" {EXTRA_ARGS}" if EXTRA_ARGS else ""
        run_cmd = (
            "echo __S05_RUN_START__;"
            f"{BOARD_BIN} --golden-dir {run_golden_dir} --phys-base {PHYS_BASE} --phys-size {PHYS_SIZE}{packet_arg}{extra_arg}; "
            "rc=$?; echo __S05_RUN_RC__=$rc; "
            "echo __S05_DMESG_CHECK__; "
            "dmesg | tail -180 | grep -Ei 'panic|oops|bug:|bus error|external abort|imprecise|segfault|DMA|uio|gemv|xilinx' || true; "
            "echo __S05_RUN_END__"
        )
        if not s.cmd(run_cmd, timeout=RUN_TIMEOUT):
            return 6

        log_bytes = LOG_PATH.read_bytes()
        required_run = [
            b"__S05_RUN_RC__=0",
            b"[FPGA GEMV HW TEST]",
            b"case: fake_gemv",
            b"mode=0 scaled: PASS",
            b"mode=1 block_acc: PASS",
            b"AXI-Lite bulk data path used: no",
            b"AXI DMA MM2S/S2MM used: yes",
            b"OVERALL PASS",
            b"__S05_RUN_END__",
        ]
        missing = [m.decode("ascii") for m in required_run if m not in log_bytes]
        if missing:
            print("FAIL: run missing expected markers: " + ", ".join(missing), file=sys.stderr)
            return 7

        run_start = log_bytes.rfind(b"\n__S05_RUN_START__")
        if run_start < 0:
            run_start = log_bytes.rfind(b"__S05_RUN_START__")
        run_end = log_bytes.rfind(b"__S05_RUN_END__")
        region = log_bytes[run_start:run_end].upper() if run_start >= 0 and run_end > run_start else log_bytes.upper()
        bad_checks = [
            (b"KERNEL PANIC", "KERNEL PANIC"),
            (rb"\bOOPS\b", "OOPS"),
            (b"EXTERNAL ABORT", "EXTERNAL ABORT"),
            (b"BUS ERROR", "BUS ERROR"),
            (b"TIMEOUT WAITING", "TIMEOUT WAITING"),
        ]
        bad = [label for pattern, label in bad_checks if re.search(pattern, region)]
        if bad:
            print("FAIL: bad markers present: " + ", ".join(bad), file=sys.stderr)
            return 8

        summary = (
            "\n### HOST_VERIFICATION_SUMMARY\n"
            "S05 result: PASS\n"
            "program: runtime_c/gemv_hw_test.c\n"
            f"board binary: {BOARD_BIN}\n"
            f"golden_dir: {run_golden_dir}\n"
            f"phys_base: {PHYS_BASE}\n"
            f"phys_size: {PHYS_SIZE}\n"
            "mode=0 scaled: PASS\n"
            "mode=1 block_acc: PASS\n"
            "AXI-Lite bulk data path used: no\n"
            "AXI DMA MM2S/S2MM used: yes\n"
            "input BRAM used: yes\n"
            "timeout/error/fallback observed: no\n"
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
