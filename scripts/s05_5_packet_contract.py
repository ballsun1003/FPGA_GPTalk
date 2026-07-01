#!/usr/bin/env python3
"""S05.5 fake_gemv packet contract checker for 32-bit and 128-bit AXIS."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "runtime_c" / "gemv_hw_test.c"
LOG32 = ROOT / "logs" / "s05_5_packet_contract_32.txt"
LOG128 = ROOT / "logs" / "s05_5_packet_contract_128.txt"

LANES = 16
Q8_BLOCK_SIZE = 32
IN_FEATURES = 32
OUT_FEATURES = 3
PADDED_OUT_FEATURES = 16
BLOCKS_PER_ROW = IN_FEATURES // Q8_BLOCK_SIZE
SCALE_BYTES = PADDED_OUT_FEATURES * BLOCKS_PER_ROW * 4
WEIGHT_BYTES = PADDED_OUT_FEATURES * IN_FEATURES
PACKET_BYTES = SCALE_BYTES + WEIGHT_BYTES


def extract_array(source: str, name: str) -> bytes:
    pattern = rf"static const uint8_t {name}\[\] = \{{(.*?)\}};"
    match = re.search(pattern, source, re.S)
    if not match:
        raise SystemExit(f"array not found: {name}")
    return bytes(int(item, 16) for item in re.findall(r"0x[0-9a-fA-F]{2}", match.group(1)))


def check_contract(axis_bytes: int, packet: bytes, phys_addr: int | None) -> tuple[str, bool]:
    axis_bits = axis_bytes * 8
    scale_beats = SCALE_BYTES // axis_bytes
    weight_beats = WEIGHT_BYTES // axis_bytes
    total_beats = len(packet) // axis_bytes
    expected_tlast = total_beats - 1
    full_tkeep = (1 << axis_bytes) - 1
    failures: list[str] = []

    def expect(name: str, got: int, expected: int) -> None:
        if got != expected:
            failures.append(f"{name}: got={got} expected={expected}")

    expect("packet_bytes", len(packet), 576)
    expect("dma_mm2s_btt_bytes", len(packet), 576)
    if axis_bits == 32:
        expect("total_beats_32", total_beats, 144)
        expect("tlast_index_32", expected_tlast, 143)
        expect("scale_beats_32", scale_beats, 16)
        expect("weight_beats_32", weight_beats, 128)
    elif axis_bits == 128:
        expect("total_beats_128", total_beats, 36)
        expect("tlast_index_128", expected_tlast, 35)
        expect("scale_beats_128", scale_beats, 4)
        expect("weight_beats_128", weight_beats, 32)
        expect("tkeep_full_128", full_tkeep, 0xFFFF)
    else:
        failures.append(f"unsupported_axis_bits={axis_bits}")

    if len(packet) % axis_bytes != 0:
        failures.append(f"packet not divisible by axis beat bytes: {axis_bytes}")
    if phys_addr is not None and phys_addr % 16 != 0:
        failures.append(f"buffer physical address not 16-byte aligned: 0x{phys_addr:x}")

    lines = [
        f"S05.5 packet contract {axis_bits}-bit",
        f"axis_data_width_bits: {axis_bits}",
        f"axis_bytes: {axis_bytes}",
        f"lanes: {LANES}",
        f"packet_bytes: {len(packet)}",
        f"dma_mm2s_btt_bytes: {len(packet)}",
        f"dma_btt_unit: bytes",
        f"scale_bytes: {SCALE_BYTES}",
        f"weight_bytes: {WEIGHT_BYTES}",
        f"scale_beats: {scale_beats}",
        f"weight_beats: {weight_beats}",
        f"total_beats: {total_beats}",
        f"tlast_index: {expected_tlast}",
        f"full_tkeep_hex: 0x{full_tkeep:0{axis_bytes // 4}x}",
        "tkeep_all_fake_gemv_beats: full",
        f"buffer_phys_addr: {'not_provided' if phys_addr is None else hex(phys_addr)}",
        f"buffer_phys_16byte_aligned: {'not_checked' if phys_addr is None else str(phys_addr % 16 == 0).lower()}",
        f"contract_check: {'PASS' if not failures else 'FAIL'}",
    ]
    if failures:
        lines.append("failures:")
        lines.extend(f"- {item}" for item in failures)
    return "\n".join(lines) + "\n", not failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phys-addr", type=lambda text: int(text, 0), default=None)
    args = parser.parse_args()

    source = SRC.read_text(encoding="ascii")
    scale = extract_array(source, "EMBEDDED_SCALE_Q_I32")
    weight = extract_array(source, "EMBEDDED_WEIGHT_Q8_FPGA_LAYOUT")
    packet = scale + weight

    LOG32.parent.mkdir(parents=True, exist_ok=True)
    text32, pass32 = check_contract(4, packet, args.phys_addr)
    text128, pass128 = check_contract(16, packet, args.phys_addr)
    LOG32.write_text(text32, encoding="ascii")
    LOG128.write_text(text128, encoding="ascii")
    print(text32.strip())
    print(text128.strip())
    return 0 if pass32 and pass128 else 1


if __name__ == "__main__":
    raise SystemExit(main())
