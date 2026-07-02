#!/usr/bin/env python3
import argparse
import datetime as dt
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_FSBL = REPO / "artifacts/s03_bootgen/zynq_fsbl.elf"
DEFAULT_BIT = REPO / "hw/vivado_project/export/GPTalk_dma.bit"
DEFAULT_XSA = REPO / "hw/vivado_project/export/GPTalk_dma.xsa"
DEFAULT_DCP = REPO / "hw/vivado_project/GPTalk.runs/design_1_gemv_q8_0_dma_top_0_0_synth_1/design_1_gemv_q8_0_dma_top_0_0.dcp"
DEFAULT_UBOOT = REPO / "artifacts/s03_bootgen/u-boot.elf"
BOOT_TESTS = REPO / "artifacts/boot_tests"
LOGS = REPO / "logs"


def default_bootgen() -> Path:
    if os.environ.get("BOOTGEN"):
        return Path(os.environ["BOOTGEN"])
    found = shutil.which("bootgen")
    if found:
        return Path(found)
    vivado_root = os.environ.get("XILINX_VIVADO") or os.environ.get("VIVADO_ROOT")
    if vivado_root:
        return Path(vivado_root) / "bin" / "bootgen"
    return Path("bootgen")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def require_file(path: Path, label: str) -> Path:
    path = path.resolve()
    if not path.is_file():
        raise SystemExit(f"missing {label}: {path}")
    return path


def run(cmd, cwd=None, log_path=None):
    if log_path:
        with log_path.open("wb") as log:
            proc = subprocess.run(cmd, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
    else:
        proc = subprocess.run(cmd, cwd=cwd)
    if proc.returncode != 0:
        rendered = " ".join(str(x) for x in cmd)
        raise SystemExit(f"command failed rc={proc.returncode}: {rendered}")


def unique_backup_name(prefix: str) -> str:
    label = "".join(c if c.isalnum() else "_" for c in prefix.upper())
    now = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return f"BOOT_BEFORE_{label}_{now}.BIN"


def write_text(path: Path, text: str):
    path.write_text(text, encoding="ascii")


def mount_partition(device: str) -> Path:
    proc = subprocess.run(
        ["udisksctl", "mount", "-b", device],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if proc.returncode != 0:
        mounted = subprocess.run(
            ["findmnt", "-nr", "-o", "TARGET", "--source", device],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        if mounted.returncode == 0 and mounted.stdout.strip():
            return Path(mounted.stdout.strip().splitlines()[0])
        raise SystemExit(proc.stdout.strip() or f"could not mount {device}")
    out = proc.stdout
    marker = " at "
    if marker not in out:
        raise SystemExit(f"could not parse udisksctl mount output: {out.strip()}")
    return Path(out.strip().split(marker, 1)[1])


def unmount_partition(device: str):
    run(["udisksctl", "unmount", "-b", device])


def stage_sd(device: str, boot_bin: Path, test_name: str) -> dict:
    mount = mount_partition(device)
    result = {
        "device": device,
        "mount": str(mount),
        "backup": "NONE",
        "backup_sha256": "NONE",
        "sd_boot_sha256": "",
        "image_ub_sha256": "NONE",
        "uenv_sha256": "NONE",
    }
    try:
        current = mount / "BOOT.BIN"
        if current.exists():
            backup = mount / unique_backup_name(test_name)
            shutil.copy2(current, backup)
            result["backup"] = f"{device}:/{backup.name}"
            result["backup_sha256"] = sha256(backup)

        shutil.copy2(boot_bin, current)
        subprocess.run(["sync", str(current)], check=True)
        subprocess.run(["sync", str(mount)], check=True)
        result["sd_boot_sha256"] = sha256(current)

        image_ub = mount / "image.ub"
        uenv = mount / "uEnv.txt"
        if image_ub.exists():
            result["image_ub_sha256"] = sha256(image_ub)
        if uenv.exists():
            result["uenv_sha256"] = sha256(uenv)

        subprocess.run(["sync"], check=True)
    finally:
        unmount_partition(device)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Package active GPTalk bitstream into BOOT.BIN and optionally stage it to an SD boot partition."
    )
    parser.add_argument("--test-name", required=True, help="Folder name under artifacts/boot_tests/")
    parser.add_argument("--purpose", default="", help="Short manifest purpose text")
    parser.add_argument("--fsbl", type=Path, default=DEFAULT_FSBL)
    parser.add_argument("--bitstream", type=Path, default=DEFAULT_BIT)
    parser.add_argument("--uboot", type=Path, default=DEFAULT_UBOOT)
    parser.add_argument("--xsa", type=Path, default=DEFAULT_XSA)
    parser.add_argument("--dcp", type=Path, default=DEFAULT_DCP)
    parser.add_argument("--bootgen", type=Path, default=default_bootgen())
    parser.add_argument("--stage-sd", metavar="/dev/sdX1", help="Optional SD boot partition to mount, copy BOOT.BIN to, verify, and unmount")
    parser.add_argument("--force", action="store_true", help="Allow overwriting an existing test folder")
    args = parser.parse_args()

    fsbl = require_file(args.fsbl, "FSBL")
    bitstream = require_file(args.bitstream, "bitstream")
    uboot = require_file(args.uboot, "U-Boot")
    bootgen = require_file(args.bootgen, "bootgen")
    xsa = args.xsa.resolve()
    dcp = args.dcp.resolve()

    test_dir = BOOT_TESTS / args.test_name
    if test_dir.exists() and not args.force:
        raise SystemExit(f"test folder already exists; use --force to replace generated files: {test_dir}")
    test_dir.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)

    local_fsbl = test_dir / "fsbl_known_good_s03.elf"
    local_bit = test_dir / "GPTalk_dma_active.bit"
    local_uboot = test_dir / "u-boot.elf"
    shutil.copy2(fsbl, local_fsbl)
    shutil.copy2(bitstream, local_bit)
    shutil.copy2(uboot, local_uboot)

    bif = test_dir / "boot.bif"
    write_text(
        bif,
        "the_ROM_image:\n"
        "{\n"
        "  [bootloader] fsbl_known_good_s03.elf\n"
        "  GPTalk_dma_active.bit\n"
        "  u-boot.elf\n"
        "}\n",
    )

    boot_log = LOGS / f"{args.test_name}_bootgen.log"
    boot_bin = test_dir / "BOOT.BIN"
    run([str(bootgen), "-arch", "zynq", "-image", "boot.bif", "-w", "-o", "BOOT.BIN"], cwd=test_dir, log_path=boot_log)
    boot_hash = sha256(boot_bin)

    sd_result = None
    sd_log = None
    if args.stage_sd:
        sd_log = LOGS / f"{args.test_name}_sd_stage.log"
        sd_result = stage_sd(args.stage_sd, boot_bin, args.test_name)
        lines = [
            f"test name: {args.test_name}",
            f"time: {dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S %Z')}",
            f"device: {sd_result['device']}",
            f"mount: {sd_result['mount']}",
            f"BOOT.BIN sha256: {boot_hash}",
            f"SD BOOT.BIN sha256: {sd_result['sd_boot_sha256']}",
            f"backup: {sd_result['backup']}",
            f"backup sha256: {sd_result['backup_sha256']}",
            f"image.ub sha256: {sd_result['image_ub_sha256']}",
            f"uEnv.txt sha256: {sd_result['uenv_sha256']}",
            "unmount: done",
            "",
        ]
        write_text(sd_log, "\n".join(lines))
        if sd_result["sd_boot_sha256"] != boot_hash:
            raise SystemExit("SD BOOT.BIN hash mismatch after staging")

    manifest = test_dir / "MANIFEST.txt"
    now = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    lines = [
        f"test name: {args.test_name}",
        "",
        "Purpose:",
        f"- {args.purpose or 'Package active GPTalk DMA bitstream into BOOT.BIN.'}",
        "",
        f"created: {now}",
        "",
        f"FSBL path: {local_fsbl.relative_to(REPO)}",
        f"FSBL source: {fsbl.relative_to(REPO) if fsbl.is_relative_to(REPO) else fsbl}",
        f"FSBL sha256: {sha256(local_fsbl)}",
        "",
        f"bitstream path: {local_bit.relative_to(REPO)}",
        f"bitstream source: {bitstream.relative_to(REPO) if bitstream.is_relative_to(REPO) else bitstream}",
        f"bitstream sha256: {sha256(local_bit)}",
        "",
        f"XSA source: {xsa.relative_to(REPO) if xsa.exists() and xsa.is_relative_to(REPO) else xsa}",
        f"XSA sha256: {sha256(xsa) if xsa.is_file() else 'NONE'}",
        "",
        f"GEMV OOC DCP path: {dcp.relative_to(REPO) if dcp.exists() and dcp.is_relative_to(REPO) else dcp}",
        f"GEMV OOC DCP sha256: {sha256(dcp) if dcp.is_file() else 'NONE'}",
        "",
        f"U-Boot path: {local_uboot.relative_to(REPO)}",
        f"U-Boot source: {uboot.relative_to(REPO) if uboot.is_relative_to(REPO) else uboot}",
        f"U-Boot sha256: {sha256(local_uboot)}",
        "",
        f"BIF path: {bif.relative_to(REPO)}",
        f"BIF sha256: {sha256(bif)}",
        "",
        f"BOOT.BIN path: {boot_bin.relative_to(REPO)}",
        f"BOOT.BIN sha256: {boot_hash}",
        "",
        "Bootgen:",
        f"- log: {boot_log.relative_to(REPO)}",
        "- result: PASS",
    ]
    if sd_result:
        lines.extend([
            "",
            "SD staging:",
            f"- target: {sd_result['device']}:/BOOT.BIN",
            f"- SD BOOT.BIN sha256: {sd_result['sd_boot_sha256']}",
            f"- previous BOOT backup: {sd_result['backup']}",
            f"- previous BOOT backup sha256: {sd_result['backup_sha256']}",
            f"- image.ub sha256: {sd_result['image_ub_sha256']}",
            f"- uEnv.txt sha256: {sd_result['uenv_sha256']}",
            f"- log: {sd_log.relative_to(REPO)}",
            "- unmount: done",
        ])
    lines.append("")
    write_text(manifest, "\n".join(lines))

    print(f"BOOT.BIN: {boot_bin}")
    print(f"BOOT.BIN sha256: {boot_hash}")
    print(f"MANIFEST: {manifest}")
    print(f"bootgen log: {boot_log}")
    if sd_result:
        print(f"SD {args.stage_sd}:/BOOT.BIN sha256: {sd_result['sd_boot_sha256']}")
        print(f"SD staging log: {sd_log}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
