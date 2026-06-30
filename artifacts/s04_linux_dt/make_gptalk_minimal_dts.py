#!/usr/bin/env python3
from pathlib import Path
import sys


DROP_TOP_LEVEL = {
    "amba_pl",
    "pcam_clk",
    "fixed_regulator_DOVDD@0",
    "fixed_regulator_AVDD@1",
    "fixed_regulator_DVDD@2",
}


NEW_AMBA_PL = """\
\tamba_pl {
\t\t#address-cells = <0x01>;
\t\t#size-cells = <0x01>;
\t\tcompatible = "simple-bus";
\t\tranges;

\t\taxi_dma@40400000 {
\t\t\tcompatible = "generic-uio";
\t\t\treg = <0x40400000 0x10000>;
\t\t};

\t\tinput_bram@42000000 {
\t\t\tcompatible = "generic-uio";
\t\t\treg = <0x42000000 0x10000>;
\t\t};

\t\thdmi_vdma@43010000 {
\t\t\tcompatible = "generic-uio";
\t\t\treg = <0x43010000 0x10000>;
\t\t};

\t\thdmi_vtc@43c10000 {
\t\t\tcompatible = "generic-uio";
\t\t\treg = <0x43c10000 0x10000>;
\t\t};

\t\thdmi_dynclk@43c20000 {
\t\t\tcompatible = "generic-uio";
\t\t\treg = <0x43c20000 0x10000>;
\t\t};

\t\tgemv_ctrl@43ca0000 {
\t\t\tcompatible = "generic-uio";
\t\t\treg = <0x43ca0000 0x1000>;
\t\t};
\t};
"""


def node_name(line: str) -> str | None:
    stripped = line.strip()
    if not stripped.endswith("{"):
        return None
    name = stripped[:-1].strip()
    if not name or name.startswith("/"):
        return None
    return name


def skip_block(lines: list[str], start: int) -> int:
    depth = 0
    for idx in range(start, len(lines)):
        depth += lines[idx].count("{")
        depth -= lines[idx].count("}")
        if idx > start and depth == 0:
            return idx + 1
    raise RuntimeError(f"unterminated block at line {start + 1}")


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: make_gptalk_minimal_dts.py <in.dts> <out.dts>", file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    lines = src.read_text().splitlines(keepends=True)

    out: list[str] = []
    idx = 0
    inserted_amba_pl = False

    while idx < len(lines):
        line = lines[idx]
        if line.startswith("\t") and not line.startswith("\t\t"):
            name = node_name(line)
            if name in DROP_TOP_LEVEL:
                idx = skip_block(lines, idx)
                continue
            if name == "chosen" and not inserted_amba_pl:
                out.append(NEW_AMBA_PL)
                out.append("\n")
                inserted_amba_pl = True

        if "bootargs =" in line:
            line = '\t\tbootargs = "console=ttyPS0,115200 earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait";\n'

        out.append(line)
        idx += 1

    if not inserted_amba_pl:
        raise RuntimeError("chosen node not found; did not insert amba_pl")

    dst.write_text("".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
