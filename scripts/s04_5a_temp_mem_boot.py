#!/usr/bin/env python3
import re
import sys
import time
from pathlib import Path

import serial


PORT = "/dev/ttyUSB1"
BAUD = 115200
LOG_PATH = Path("logs/s04_5a_temp_mem_boot.txt")
BASELINE_BOOTARGS = (
    "console=ttyPS0,115200 earlyprintk clk_ignore_unused "
    "uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait"
)


PROMPT_RE = re.compile(rb"(?:root@Zybo-Z7-20:~#|Zynq>|=>)")
UBOOT_PROMPT_RE = re.compile(rb"(?:Zynq>|=>)")
LINUX_PROMPT_RE = re.compile(rb"root@Zybo-Z7-20:~#")


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
        if isinstance(text, str):
            data = text.encode("ascii")
        else:
            data = text
        self.ser.write(data)
        self.ser.flush()

    def read_for(self, seconds, echo=True):
        end = time.monotonic() + seconds
        out = bytearray()
        while time.monotonic() < end:
            data = self.ser.read(4096)
            if data:
                self.log.write(data)
                self.log.flush()
                self.buf.extend(data)
                out.extend(data)
                if echo:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
            else:
                time.sleep(0.02)
        return bytes(out)

    def clear_buffer(self):
        self.buf.clear()

    def read_until(self, predicate, timeout, echo=True, tick=None):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            data = self.ser.read(4096)
            if data:
                self.log.write(data)
                self.log.flush()
                self.buf.extend(data)
                if echo:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                if predicate(bytes(self.buf)):
                    return True
            else:
                if tick:
                    tick()
                time.sleep(0.02)
        return False

    def cmd(self, command, wait_re=PROMPT_RE, timeout=20):
        marker = f"\n\n### HOST_SEND: {command}\n".encode("ascii", "replace")
        self.log.write(marker)
        self.log.flush()
        self.clear_buffer()
        self.write(command + "\r")
        ok = self.read_until(lambda b: wait_re.search(b[-65536:]) is not None, timeout)
        if not ok:
            print(f"\nTIMEOUT waiting after command: {command}", file=sys.stderr)
        return ok


def main():
    s = Session()
    interrupted = False
    try:
        print(f"Logging to {LOG_PATH}")
        s.write("\r")
        s.read_for(1)

        already_uboot = UBOOT_PROMPT_RE.search(bytes(s.buf[-4096:])) is not None
        already_linux = LINUX_PROMPT_RE.search(bytes(s.buf[-4096:])) is not None
        if not already_uboot and already_linux:
            print("\nRebooting board to interrupt U-Boot...")
            s.cmd("reboot", wait_re=re.compile(rb"(?:Restarting system|reboot: Restarting|U-Boot|Hit any key|Zynq>|=>)"), timeout=8)
        elif already_uboot:
            print("\nAlready at U-Boot prompt.")
        else:
            print("\nNo live Linux/U-Boot prompt detected. Waiting for reset into U-Boot.")

        def spam_interrupt():
            nonlocal interrupted
            recent = bytes(s.buf[-4096:])
            if b"Hit any key" in recent or b"U-Boot" in recent:
                s.write("\r")
                interrupted = True

        if already_uboot:
            ok = True
        else:
            s.clear_buffer()
            ok = s.read_until(lambda b: UBOOT_PROMPT_RE.search(b[-8192:]) is not None, 180, tick=spam_interrupt)
        if not ok:
            print("\nDid not reach U-Boot prompt. Trying additional interrupt spam.", file=sys.stderr)
            for _ in range(30):
                s.write("\r")
                time.sleep(0.1)
                s.read_for(0.1)
                if UBOOT_PROMPT_RE.search(bytes(s.buf[-8192:])):
                    ok = True
                    break
        if not ok:
            print("\nFAIL: U-Boot prompt not reached", file=sys.stderr)
            return 2

        print("\nReached U-Boot prompt.")
        s.cmd("printenv bootargs", wait_re=UBOOT_PROMPT_RE, timeout=8)
        s.cmd("printenv bootcmd", wait_re=UBOOT_PROMPT_RE, timeout=8)
        s.cmd("bdinfo", wait_re=UBOOT_PROMPT_RE, timeout=8)
        s.cmd("printenv", wait_re=UBOOT_PROMPT_RE, timeout=20)
        s.cmd(f"setenv bootargs {BASELINE_BOOTARGS} mem=960M", wait_re=UBOOT_PROMPT_RE, timeout=8)
        s.cmd("printenv bootargs", wait_re=UBOOT_PROMPT_RE, timeout=8)
        s.cmd("run cp_kernel2ram", wait_re=UBOOT_PROMPT_RE, timeout=20)

        print("\nBooting with temporary mem=960M; no saveenv was issued.")
        s.clear_buffer()
        s.write("bootm ${netstart}\r")
        linux_ok = s.read_until(lambda b: LINUX_PROMPT_RE.search(b[-8192:]) is not None, 120)
        if not linux_ok:
            print("\nFAIL: Linux root prompt not reached with mem=960M", file=sys.stderr)
            return 3

        diag = (
            "echo __S04_5A_LINUX_DIAG_START__;"
            "cat /proc/cmdline;"
            "echo __MEMINFO__;"
            "cat /proc/meminfo;"
            "echo __IOMEM__;"
            "cat /proc/iomem;"
            "echo __UIO__;"
            "for d in /sys/class/uio/uio*; do echo UIO=$(basename \"$d\") name=$(cat \"$d/name\" 2>/dev/null); "
            "for m in \"$d\"/maps/map*; do [ -e \"$m\" ] && echo map=$(basename \"$m\") addr=$(cat \"$m/addr\") size=$(cat \"$m/size\"); done; done;"
            "echo __DMESG_MEM__;"
            "dmesg | grep -Ei \"Memory|memblock|Reserved|CMA|DMA|uio\";"
            "echo __S04_5A_LINUX_DIAG_END__"
        )
        s.cmd(diag, wait_re=LINUX_PROMPT_RE, timeout=30)
        print("\nS04.5A serial procedure complete.")
        return 0
    finally:
        s.close()


if __name__ == "__main__":
    raise SystemExit(main())
