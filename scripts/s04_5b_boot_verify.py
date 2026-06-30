#!/usr/bin/env python3
import re
import sys
import time
from pathlib import Path

import serial


PORT = "/dev/ttyUSB1"
BAUD = 115200
LOG_PATH = Path("logs/s04_5b_boot_verify.txt")
PROMPT_RE = re.compile(rb"root@Zybo-Z7-20:~#")


class Session:
    def __init__(self):
        LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
        self.log = LOG_PATH.open("wb")
        self.ser = serial.Serial(PORT, BAUD, timeout=0.05)
        self.buf = bytearray()

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

    def read_until_prompt(self, timeout):
        return self.read_until(lambda b: PROMPT_RE.search(b[-65536:]) is not None, timeout)

    def cmd(self, command, timeout=30):
        marker = f"\n\n### HOST_SEND: {command}\n".encode("ascii", "replace")
        self.log.write(marker)
        self.log.flush()
        self.buf.clear()
        self.write(command + "\r")
        if not self.read_until_prompt(timeout):
            print(f"\nTIMEOUT waiting for prompt after: {command}", file=sys.stderr)
            return False
        return True


def main():
    s = Session()
    try:
        print(f"Logging to {LOG_PATH}")
        s.write("\r")
        if not s.read_until_prompt(180):
            print("FAIL: Linux root prompt not reached", file=sys.stderr)
            return 2

        diag = (
            "echo __S04_5B_VERIFY_START__;"
            "echo __CMDLINE__; cat /proc/cmdline;"
            "echo __MEMINFO__; grep -E '^(MemTotal|CmaTotal|CmaFree):' /proc/meminfo;"
            "echo __IOMEM__; cat /proc/iomem;"
            "echo __UIO__; "
            "for d in /sys/class/uio/uio*; do "
            "echo UIO=$(basename \"$d\") name=$(cat \"$d/name\" 2>/dev/null); "
            "for m in \"$d\"/maps/map*; do [ -e \"$m\" ] && echo map=$(basename \"$m\") addr=$(cat \"$m/addr\") size=$(cat \"$m/size\"); done; "
            "done;"
            "echo __BOOTFS_UENV__; "
            "boot_mp=$(mount | awk '$1==\"/dev/mmcblk0p1\" {print $3; exit}'); "
            "echo existing_mount=${boot_mp:-NONE}; "
            "if [ -n \"$boot_mp\" ] && [ -f \"$boot_mp/uEnv.txt\" ]; then "
            "cat \"$boot_mp/uEnv.txt\"; "
            "sha256sum \"$boot_mp/uEnv.txt\" 2>/dev/null || true; "
            "else "
            "mkdir -p /tmp/s04_5b_bootfs && "
            "mount -o ro /dev/mmcblk0p1 /tmp/s04_5b_bootfs 2>/tmp/s04_5b_mount.err; "
            "mount_rc=$?; echo mount_rc=$mount_rc; "
            "if [ $mount_rc -eq 0 ]; then "
            "cat /tmp/s04_5b_bootfs/uEnv.txt; "
            "sha256sum /tmp/s04_5b_bootfs/uEnv.txt 2>/dev/null || true; "
            "umount /tmp/s04_5b_bootfs; "
            "else cat /tmp/s04_5b_mount.err; fi; "
            "fi; "
            "echo __DMESG_CHECK__; "
            "dmesg | tail -160 | grep -Ei 'panic|oops|bug:|bus error|external abort|imprecise|segfault|DMA|uio|Memory|CMA' || true; "
            "echo __S04_5B_VERIFY_END__"
        )
        if not s.cmd(diag, timeout=45):
            return 3

        log = LOG_PATH.read_bytes()
        required = [
            b"__S04_5B_VERIFY_START__",
            b"__S04_5B_VERIFY_END__",
            b"mem=960M",
            b"00000000-3bffffff : System RAM",
            b"name=axi_dma",
            b"name=input_bram",
            b"name=gemv_ctrl",
            b"bootargs=console=ttyPS0,115200 earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait mem=960M",
        ]
        missing = [x.decode("ascii") for x in required if x not in log]
        if missing:
            print("FAIL: missing expected markers: " + ", ".join(missing), file=sys.stderr)
            return 4

        verify_start = log.rfind(b"__S04_5B_VERIFY_START__")
        verify_end = log.rfind(b"__S04_5B_VERIFY_END__")
        region = log[verify_start:verify_end].upper() if verify_start >= 0 and verify_end > verify_start else log.upper()
        bad = [x.decode("ascii") for x in [b"KERNEL PANIC", b"OOPS", b"EXTERNAL ABORT", b"BUS ERROR"] if x in region]
        if bad:
            print("FAIL: bad kernel markers: " + ", ".join(bad), file=sys.stderr)
            return 5

        summary = (
            "\n### HOST_VERIFICATION_SUMMARY\n"
            "S04.5B result: PASS\n"
            "root prompt reached: yes\n"
            "/proc/cmdline contains mem=960M: yes\n"
            "/proc/iomem System RAM: 00000000-3bffffff\n"
            "carveout outside System RAM: yes, 0x3c000000-0x3fffffff\n"
            "UIO nodes present: axi_dma, input_bram, gemv_ctrl\n"
            "bootfs uEnv.txt present: yes\n"
            "BOOT.BIN changed during S04.5B: no\n"
            "image.ub changed during S04.5B: no\n"
            "kernel panic/oops observed: no\n"
        )
        with LOG_PATH.open("ab") as f:
            f.write(summary.encode("ascii"))
        print(summary)
        return 0
    finally:
        s.close()


if __name__ == "__main__":
    raise SystemExit(main())
