#!/usr/bin/env python3
"""Generate S05.6.1 deterministic multi-block GEMV regression vectors."""

from __future__ import annotations

import csv
import struct
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GOLDEN_ROOT = ROOT / "golden" / "s05_6_1_multiblock"
LOG_PATH = ROOT / "logs" / "s05_6_1_multiblock_reference.txt"
CSV_PATH = ROOT / "reports" / "s05_6_1_multiblock_expected.csv"

LANES = 16
Q8_BLOCK_SIZE = 32
SCALE_SHIFT = 20
AXIS_BYTES = 16
DMA_LENGTH_WIDTH = 14
DMA_MAX_SIMPLE_BYTES = (1 << DMA_LENGTH_WIDTH) - 1


@dataclass(frozen=True)
class Case:
    shape: str
    pattern: str
    in_features: int
    out_features: int

    @property
    def name(self) -> str:
        return f"{self.shape}_{self.pattern}"

    @property
    def blocks_per_row(self) -> int:
        return self.in_features // Q8_BLOCK_SIZE

    @property
    def row_groups(self) -> int:
        return (self.out_features + LANES - 1) // LANES


def sat_i32(v: int) -> int:
    return max(-(1 << 31), min((1 << 31) - 1, v))


def round_shift_i64(value: int, shift: int) -> int:
    if shift == 0:
        return value
    rounding = 1 << (shift - 1)
    if value >= 0:
        return (value + rounding) >> shift
    return -(((-value) + rounding) >> shift)


def input_value(pattern: str, col: int) -> int:
    block = col // Q8_BLOCK_SIZE
    local = col % Q8_BLOCK_SIZE
    if pattern in {"P6", "P7", "P8", "P9"}:
        return ((col * 5 + 11) % 23) - 11
    if pattern == "P4":
        return (block * 2) + ((local % 5) - 2)
    return ((col * 5 + 11) % 11) - 5


def scale_value(pattern: str, row: int, block: int, blocks_per_row: int) -> int:
    if pattern == "P1" and block != 0:
        return 0
    if pattern == "P2" and block != blocks_per_row - 1:
        return 0
    if pattern == "P7" and block != blocks_per_row - 1:
        return 0
    if pattern == "P8" and block != blocks_per_row - 2:
        return 0
    if pattern == "P9" and block < blocks_per_row - 2:
        return 0
    if pattern == "P3":
        return (1 << SCALE_SHIFT) * (1 << (block % 3))
    return 1 << SCALE_SHIFT


def weight_value(pattern: str, row: int, col: int, blocks_per_row: int) -> int:
    block = col // Q8_BLOCK_SIZE
    local = col % Q8_BLOCK_SIZE
    if pattern == "P1" and block != 0:
        return 0
    if pattern == "P2" and block != blocks_per_row - 1:
        return 0
    if pattern in {"P6", "P7", "P8", "P9"}:
        if pattern == "P7" and block != blocks_per_row - 1:
            return 0
        if pattern == "P8" and block != blocks_per_row - 2:
            return 0
        if pattern == "P9" and block < blocks_per_row - 2:
            return 0
        return ((row * 13 + col * 7 + 3) % 17) - 8
    if pattern == "P5":
        return ((row * 3 + local * 2 + block) % 7) - 3
    return ((row * 3 + local * 2 + block * 5 + 1) % 7) - 3


def pack_i16(values: list[int]) -> bytes:
    return b"".join(struct.pack("<h", v) for v in values)


def pack_i32(values: list[int]) -> bytes:
    return b"".join(struct.pack("<i", v) for v in values)


def write_hex(path: Path, values: list[int], width: int) -> None:
    mask = (1 << width) - 1
    digits = width // 4
    path.write_text("".join(f"{v & mask:0{digits}x}\n" for v in values), encoding="ascii")


def generate_case(case: Case) -> dict[str, int | str]:
    assert case.in_features % Q8_BLOCK_SIZE == 0
    assert case.out_features == LANES

    case_dir = GOLDEN_ROOT / case.name
    case_dir.mkdir(parents=True, exist_ok=True)

    inputs = [input_value(case.pattern, col) for col in range(case.in_features)]
    scales: list[int] = []
    packet = bytearray()
    block_acc_flat: list[int] = []
    scaled_flat: list[int] = []
    output = [0 for _ in range(case.out_features)]

    for group in range(case.row_groups):
        row_base = group * LANES
        for block in range(case.blocks_per_row):
            block_scales: list[int] = []
            for lane in range(LANES):
                row = row_base + lane
                scale = scale_value(case.pattern, row, block, case.blocks_per_row)
                if row >= case.out_features:
                    scale = 0
                block_scales.append(scale)
                scales.append(scale)
                packet.extend(struct.pack("<i", scale))

            block_accs = [0 for _ in range(LANES)]
            for local_col in range(Q8_BLOCK_SIZE):
                col = block * Q8_BLOCK_SIZE + local_col
                for lane in range(LANES):
                    row = row_base + lane
                    w = weight_value(case.pattern, row, col, case.blocks_per_row)
                    if row >= case.out_features:
                        w = 0
                    packet.extend(struct.pack("<b", w))
                    block_accs[lane] += inputs[col] * w

            for lane, block_acc in enumerate(block_accs):
                row = row_base + lane
                if row >= case.out_features:
                    continue
                scaled = round_shift_i64(block_acc * block_scales[lane], SCALE_SHIFT)
                block_acc_flat.append(block_acc)
                scaled_flat.append(scaled)
                output[row] = sat_i32(output[row] + scaled)

    packet_bytes = bytes(packet)
    if len(packet_bytes) % AXIS_BYTES != 0:
        raise RuntimeError(f"{case.name}: packet not aligned to {AXIS_BYTES} bytes")

    input_bytes = pack_i16(inputs)
    output_bytes = pack_i32(output)
    block_acc_bytes = pack_i32(block_acc_flat)
    scaled_bytes = pack_i32(scaled_flat)
    scale_bytes = pack_i32(scales)

    (case_dir / "input_i16.bin").write_bytes(input_bytes)
    (case_dir / "packet_axis128.bin").write_bytes(packet_bytes)
    (case_dir / "scale_q_i32.bin").write_bytes(scale_bytes)
    (case_dir / "output_mode0_i32.bin").write_bytes(output_bytes)
    (case_dir / "block_acc_i32.bin").write_bytes(block_acc_bytes)
    (case_dir / "scaled_block_i32.bin").write_bytes(scaled_bytes)
    write_hex(case_dir / "input_i16.hex", [v & 0xFFFF for v in inputs], 16)
    write_hex(case_dir / "packet_axis128.hex", list(packet_bytes), 8)
    write_hex(case_dir / "output_mode0_i32.hex", [v & 0xFFFFFFFF for v in output], 32)
    write_hex(case_dir / "block_acc_i32.hex", [v & 0xFFFFFFFF for v in block_acc_flat], 32)
    write_hex(case_dir / "scaled_block_i32.hex", [v & 0xFFFFFFFF for v in scaled_flat], 32)

    first_expected = output[0]
    last_expected = output[-1]
    packet_limit_ok = len(packet_bytes) <= DMA_MAX_SIMPLE_BYTES
    (case_dir / "MANIFEST.txt").write_text(
        "\n".join(
            [
                f"name: {case.name}",
                f"shape: {case.in_features}x{case.out_features}",
                f"pattern: {case.pattern}",
                f"blocks_per_row: {case.blocks_per_row}",
                f"row_groups: {case.row_groups}",
                f"packet_bytes: {len(packet_bytes)}",
                f"axis_width_bits: {AXIS_BYTES * 8}",
                f"dma_simple_limit_bytes: {DMA_MAX_SIMPLE_BYTES}",
                f"dma_full_transfer_possible: {str(packet_limit_ok).lower()}",
                f"output0: {first_expected}",
                f"output15: {last_expected}",
                "",
            ]
        ),
        encoding="ascii",
    )

    return {
        "case": case.name,
        "shape": f"{case.in_features}x{case.out_features}",
        "pattern": case.pattern,
        "in_features": case.in_features,
        "out_features": case.out_features,
        "blocks_per_row": case.blocks_per_row,
        "row_groups": case.row_groups,
        "packet_bytes": len(packet_bytes),
        "axis128_beats": len(packet_bytes) // AXIS_BYTES,
        "expected_tlast_beat": (len(packet_bytes) // AXIS_BYTES) - 1,
        "dma_simple_limit_bytes": DMA_MAX_SIMPLE_BYTES,
        "dma_full_transfer_possible": str(packet_limit_ok).lower(),
        "output0": first_expected,
        "output15": last_expected,
        "golden_dir": str(case_dir.relative_to(ROOT)),
    }


def build_cases() -> list[Case]:
    cases: list[Case] = []
    cases.append(Case("A_32x16", "P0", 32, 16))
    for pattern in ["P0", "P1", "P2", "P3", "P4", "P5"]:
        cases.append(Case("B_64x16", pattern, 64, 16))
    for pattern in ["P0", "P1", "P2", "P3"]:
        cases.append(Case("C_96x16", pattern, 96, 16))
    cases.append(Case("D_512x16", "P0", 512, 16))
    cases.append(Case("D_512x16", "P6", 512, 16))
    cases.append(Case("D2_544x16", "P6", 544, 16))
    for pattern in ["P0", "P1", "P2", "P3"]:
        cases.append(Case("E_576x16", pattern, 576, 16))
    for pattern in ["P6", "P7", "P8", "P9"]:
        cases.append(Case("E_576x16", pattern, 576, 16))
    cases.append(Case("F_1536x16", "DEFER_DMA_LENGTH", 1536, 16))
    return cases


def main() -> int:
    GOLDEN_ROOT.mkdir(parents=True, exist_ok=True)
    LOG_PATH.parent.mkdir(parents=True, exist_ok=True)
    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)

    rows = []
    log_lines = ["S05.6.1 multi-block deterministic reference"]
    for case in build_cases():
        if case.pattern == "DEFER_DMA_LENGTH":
            packet_bytes = full_packet_bytes = (
                case.row_groups
                * case.blocks_per_row
                * (LANES * 4 + LANES * Q8_BLOCK_SIZE)
            )
            row = {
                "case": case.name,
                "shape": f"{case.in_features}x{case.out_features}",
                "pattern": case.pattern,
                "in_features": case.in_features,
                "out_features": case.out_features,
                "blocks_per_row": case.blocks_per_row,
                "row_groups": case.row_groups,
                "packet_bytes": packet_bytes,
                "axis128_beats": packet_bytes // AXIS_BYTES,
                "expected_tlast_beat": (packet_bytes // AXIS_BYTES) - 1,
                "dma_simple_limit_bytes": DMA_MAX_SIMPLE_BYTES,
                "dma_full_transfer_possible": str(packet_bytes <= DMA_MAX_SIMPLE_BYTES).lower(),
                "output0": "DEFER",
                "output15": "DEFER",
                "golden_dir": "DEFER_S05_6_2_DMA_LENGTH_WIDTH",
            }
            rows.append(row)
            log_lines.append(
                f"{case.name}: DEFER packet_bytes={full_packet_bytes} limit={DMA_MAX_SIMPLE_BYTES}"
            )
            continue

        row = generate_case(case)
        rows.append(row)
        log_lines.append(
            f"{row['case']}: packet_bytes={row['packet_bytes']} "
            f"beats={row['axis128_beats']} tlast={row['expected_tlast_beat']} "
            f"dma_ok={row['dma_full_transfer_possible']} out0={row['output0']} out15={row['output15']}"
        )

    fieldnames = list(rows[0].keys())
    with CSV_PATH.open("w", newline="", encoding="ascii") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    LOG_PATH.write_text("\n".join(log_lines) + "\n", encoding="ascii")
    print(LOG_PATH)
    print(CSV_PATH)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
