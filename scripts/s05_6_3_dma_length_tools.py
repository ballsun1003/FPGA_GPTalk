#!/usr/bin/env python3
"""Generate S05.6.3 DMA length/chunking audit artifacts."""

from __future__ import annotations

import csv
import hashlib
import math
import subprocess
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LANES = 16
Q8_BLOCK_SIZE = 32
SCALE_SHIFT = 20
AXIS_BYTES = 16
DMA_LENGTH_WIDTH = 14
DMA_MAX_SIMPLE_BYTES = (1 << DMA_LENGTH_WIDTH) - 1
BYTES_PER_BLOCK_ROWGROUP = LANES * 4 + LANES * Q8_BLOCK_SIZE

S05_4_CSV = ROOT / "reports" / "s05_4_real_workload_throughput_model.csv"
CANDIDATE_TAG = "s05_6_2_mode1_isolated_identity_scale_74MHz"
CANDIDATE_BIT = ROOT / "hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.bit"
CANDIDATE_XSA = ROOT / "hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.xsa"
CANDIDATE_BOOT = (
    ROOT
    / "artifacts/boot_tests/test_s05_6_2_mode1_isolated_identity_scale_74mhz_s03_fsbl_s03_uboot/BOOT.BIN"
)
CANDIDATE_GATE = ROOT / "logs/s05_6_2_mode1_isolated_identity_scale_74mhz_board_gate_summary.txt"
DMA_XCI = (
    ROOT
    / "hw/vivado_project/GPTalk.srcs/sources_1/bd/design_1/ip/design_1_axi_dma_0_0/design_1_axi_dma_0_0.xci"
)


@dataclass(frozen=True)
class Pattern:
    name: str


def run_text(args: list[str]) -> str:
    try:
        return subprocess.check_output(args, cwd=ROOT, text=True, stderr=subprocess.STDOUT).strip()
    except subprocess.CalledProcessError as exc:
        return exc.output.strip()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def active_source_hash() -> tuple[str, list[str]]:
    patterns = [
        "runtime_c/*.c",
        "runtime_c/CMakeLists.txt",
        "scripts/*.py",
        "scripts/*.sh",
        "scripts/*.tcl",
        "vivado_ip/rtl/*.v",
        "vivado_ip/tb/*.sv",
        "hw/vivado_project/GPTalk.xpr",
        "hw/vivado_project/GPTalk.srcs/sources_1/bd/design_1/design_1.bd",
        "hw/vivado_project/GPTalk.srcs/sources_1/bd/design_1/ip/design_1_axi_dma_0_0/design_1_axi_dma_0_0.xci",
        "docs/00_ACTIVE_KR.md",
        "docs/s05_4_real_workload_throughput_model.md",
        "docs/s05_6_batching_persistent_job.md",
        "docs/s05_6_1_multiblock_correctness.md",
        "docs/s05_6_2_debug_status.md",
    ]
    files: list[Path] = []
    for pattern in patterns:
        files.extend(sorted(ROOT.glob(pattern)))
    files = sorted(set(p for p in files if p.is_file()))
    h = hashlib.sha256()
    rels: list[str] = []
    for path in files:
        rel = path.relative_to(ROOT).as_posix()
        rels.append(rel)
        h.update(rel.encode("ascii"))
        h.update(b"\0")
        h.update(sha256_file(path).encode("ascii"))
        h.update(b"\n")
    return h.hexdigest(), rels


def parse_dma_xci() -> dict[str, str]:
    text = DMA_XCI.read_text(encoding="ascii", errors="replace")
    out: dict[str, str] = {}
    for key in [
        "c_sg_length_width",
        "C_SG_LENGTH_WIDTH",
        "c_include_sg",
        "C_INCLUDE_SG",
        "c_include_mm2s",
        "c_include_s2mm",
        "c_m_axis_mm2s_tdata_width",
        "C_M_AXIS_MM2S_TDATA_WIDTH",
    ]:
        needle = f'"{key}"'
        idx = text.find(needle)
        if idx < 0:
            continue
        value_idx = text.find('"value"', idx)
        colon_idx = text.find(":", value_idx)
        quote_1 = text.find('"', colon_idx)
        quote_2 = text.find('"', quote_1 + 1)
        if value_idx >= 0 and colon_idx >= 0 and quote_1 >= 0 and quote_2 >= 0:
            out[key] = text[quote_1 + 1 : quote_2]
    return out


def packet_bytes_for(in_features: int, out_features: int) -> tuple[int, int, int]:
    blocks = in_features // Q8_BLOCK_SIZE
    row_groups = (out_features + LANES - 1) // LANES
    rowgroup_packet = blocks * BYTES_PER_BLOCK_ROWGROUP
    return blocks, row_groups, rowgroup_packet


def required_width(bytes_count: int) -> int:
    return math.ceil(math.log2(bytes_count + 1))


def input_value(pattern: Pattern, col: int) -> int:
    if pattern.name == "P6":
        return ((col * 5 + 11) % 23) - 11
    return ((col * 5 + 11) % 11) - 5


def weight_value(pattern: Pattern, row: int, col: int, blocks_per_row: int) -> int:
    block = col // Q8_BLOCK_SIZE
    local = col % Q8_BLOCK_SIZE
    if pattern.name == "P6":
        return ((row * 13 + col * 7 + 3) % 17) - 8
    return ((row * 3 + local * 2 + block * 5 + 1) % 7) - 3


def scale_value(_pattern: Pattern, _row: int, _block: int, _blocks_per_row: int) -> int:
    return 1 << SCALE_SHIFT


def sat_i32(v: int) -> int:
    return max(-(1 << 31), min((1 << 31) - 1, v))


def round_shift_i64(value: int, shift: int) -> int:
    if shift == 0:
        return value
    rounding = 1 << (shift - 1)
    if value >= 0:
        return (value + rounding) >> shift
    return -(((-value) + rounding) >> shift)


def full_ref(pattern: Pattern, in_features: int, out_features: int) -> list[int]:
    blocks = in_features // Q8_BLOCK_SIZE
    out = [0 for _ in range(out_features)]
    for row in range(out_features):
        row_acc = 0
        for block in range(blocks):
            block_acc = 0
            for local in range(Q8_BLOCK_SIZE):
                col = block * Q8_BLOCK_SIZE + local
                block_acc += input_value(pattern, col) * weight_value(pattern, row, col, blocks)
            scaled = round_shift_i64(block_acc * scale_value(pattern, row, block, blocks), SCALE_SHIFT)
            row_acc = sat_i32(row_acc + scaled)
        out[row] = row_acc
    return out


def chunked_ref(pattern: Pattern, in_features: int, out_features: int, chunk: int) -> list[int]:
    full_blocks = in_features // Q8_BLOCK_SIZE
    acc = [0 for _ in range(out_features)]
    for chunk_base in range(0, in_features, chunk):
        chunk_blocks = chunk // Q8_BLOCK_SIZE
        chunk_out = [0 for _ in range(out_features)]
        for row in range(out_features):
            row_acc = 0
            for local_block in range(chunk_blocks):
                global_block = chunk_base // Q8_BLOCK_SIZE + local_block
                block_acc = 0
                for local in range(Q8_BLOCK_SIZE):
                    col = chunk_base + local_block * Q8_BLOCK_SIZE + local
                    block_acc += input_value(pattern, col) * weight_value(pattern, row, col, full_blocks)
                scaled = round_shift_i64(
                    block_acc * scale_value(pattern, row, global_block, full_blocks), SCALE_SHIFT
                )
                row_acc = sat_i32(row_acc + scaled)
            chunk_out[row] = row_acc
        for row, value in enumerate(chunk_out):
            acc[row] += value
    return [sat_i32(v) for v in acc]


def generate_baseline() -> None:
    source_hash, source_files = active_source_hash()
    dma = parse_dma_xci()
    gate_summary = CANDIDATE_GATE.read_text(encoding="ascii", errors="replace")
    gate_lines = [
        line
        for line in gate_summary.splitlines()
        if "BOARD_GATE_PASS" in line
        or "S06_GATE" in line
        or line.startswith("tag:")
        or line.startswith("expected BOOT sha256:")
        or line.startswith("NOTE:")
    ]
    out = [
        "S05.6.3 baseline freeze",
        f"candidate_tag: {CANDIDATE_TAG}",
        f"candidate_bitstream_path: {CANDIDATE_BIT.relative_to(ROOT)}",
        f"candidate_bitstream_sha256: {sha256_file(CANDIDATE_BIT)}",
        f"candidate_xsa_path: {CANDIDATE_XSA.relative_to(ROOT)}",
        f"candidate_xsa_sha256: {sha256_file(CANDIDATE_XSA)}",
        f"candidate_boot_path: {CANDIDATE_BOOT.relative_to(ROOT)}",
        f"candidate_boot_sha256: {sha256_file(CANDIDATE_BOOT)}",
        f"git_head: {run_text(['git', 'rev-parse', 'HEAD'])}",
        f"git_branch: {run_text(['git', 'branch', '--show-current'])}",
        f"git_status_short_sha256: {hashlib.sha256(run_text(['git', 'status', '--short']).encode()).hexdigest()}",
        f"active_source_manifest_sha256: {source_hash}",
        f"active_source_file_count: {len(source_files)}",
        f"s05_4_workload_model_path: {S05_4_CSV.relative_to(ROOT)}",
        f"s05_4_workload_model_sha256: {sha256_file(S05_4_CSV)}",
        f"dma_xci_path: {DMA_XCI.relative_to(ROOT)}",
        f"current_C_SG_LENGTH_WIDTH: {dma.get('C_SG_LENGTH_WIDTH', 'UNKNOWN')}",
        f"current_c_include_sg: {dma.get('c_include_sg', 'UNKNOWN')}",
        f"current_max_btt_bytes: {DMA_MAX_SIMPLE_BYTES}",
        "",
        "S05.6.2 board gate summary excerpt:",
        *gate_lines,
        "",
        "Active source files included in source hash:",
        *source_files,
        "",
    ]
    path = ROOT / "logs/s05_6_3_baseline_freeze.txt"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(out), encoding="ascii")


def generate_packet_audit() -> None:
    out_path = ROOT / "reports/s05_6_3_rowgroup_packet_sizes.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with S05_4_CSV.open(newline="", encoding="ascii") as f:
        rows = list(csv.DictReader(f))
    fields = [
        "tensor_name",
        "target",
        "in_features",
        "out_features",
        "blocks_per_row",
        "row_groups",
        "rowgroup_packet_bytes",
        "current_dma_limit_ok",
        "needs_chunking",
        "required_btt_width_bits",
    ]
    with out_path.open("w", newline="", encoding="ascii") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            in_features = int(row["in_features"])
            out_features = int(row["out_features"])
            blocks, row_groups, rowgroup_packet = packet_bytes_for(in_features, out_features)
            writer.writerow(
                {
                    "tensor_name": row["tensor_name"],
                    "target": row["target"],
                    "in_features": in_features,
                    "out_features": out_features,
                    "blocks_per_row": blocks,
                    "row_groups": row_groups,
                    "rowgroup_packet_bytes": rowgroup_packet,
                    "current_dma_limit_ok": str(rowgroup_packet <= DMA_MAX_SIMPLE_BYTES).lower(),
                    "needs_chunking": str(in_features == 1536).lower(),
                    "required_btt_width_bits": required_width(rowgroup_packet),
                }
            )


def generate_chunk_policy() -> None:
    out_path = ROOT / "reports/s05_6_3_tensor_chunk_policy.csv"
    with S05_4_CSV.open(newline="", encoding="ascii") as f:
        rows = list(csv.DictReader(f))
    fields = [
        "tensor_name",
        "target",
        "in_features",
        "out_features",
        "row_groups",
        "original_rowgroup_packet_bytes",
        "chunk_count",
        "chunk_in_features",
        "chunk_packet_bytes",
        "dma_limit_ok",
        "cpu_accumulation_required",
    ]
    with out_path.open("w", newline="", encoding="ascii") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            in_features = int(row["in_features"])
            out_features = int(row["out_features"])
            _blocks, row_groups, original_packet = packet_bytes_for(in_features, out_features)
            if in_features <= 576:
                chunk_count = 1
                chunk_in = in_features
            elif in_features == 1536:
                chunk_count = 3
                chunk_in = 512
            else:
                chunk_count = math.ceil(in_features / 512)
                chunk_in = 512
            chunk_blocks, _chunk_groups, chunk_packet = packet_bytes_for(chunk_in, LANES)
            if chunk_blocks == 0:
                raise RuntimeError("invalid chunk policy")
            writer.writerow(
                {
                    "tensor_name": row["tensor_name"],
                    "target": row["target"],
                    "in_features": in_features,
                    "out_features": out_features,
                    "row_groups": row_groups,
                    "original_rowgroup_packet_bytes": original_packet,
                    "chunk_count": chunk_count,
                    "chunk_in_features": chunk_in,
                    "chunk_packet_bytes": chunk_packet,
                    "dma_limit_ok": str(chunk_packet <= DMA_MAX_SIMPLE_BYTES).lower(),
                    "cpu_accumulation_required": str(chunk_count > 1).lower(),
                }
            )


def generate_reference() -> None:
    csv_path = ROOT / "reports/s05_6_3_chunked_reference.csv"
    log_path = ROOT / "logs/s05_6_3_chunked_reference.txt"
    fields = [
        "case",
        "pattern",
        "in_features",
        "out_features",
        "chunk_count",
        "chunk_in_features",
        "full_packet_bytes",
        "chunk_packet_bytes",
        "max_abs_diff",
        "mismatch_count",
        "result",
    ]
    rows = []
    log = ["S05.6.3 chunked reference validation"]
    for pattern in [Pattern("P0"), Pattern("P6")]:
        full = full_ref(pattern, 1536, 16)
        chunked = chunked_ref(pattern, 1536, 16, 512)
        diffs = [abs(a - b) for a, b in zip(full, chunked)]
        mismatch_count = sum(1 for a, b in zip(full, chunked) if a != b)
        _blocks, _groups, full_packet = packet_bytes_for(1536, 16)
        _cblocks, _cgroups, chunk_packet = packet_bytes_for(512, 16)
        result = "PASS" if mismatch_count == 0 else "FAIL"
        rows.append(
            {
                "case": f"F_1536x16_{pattern.name}",
                "pattern": pattern.name,
                "in_features": 1536,
                "out_features": 16,
                "chunk_count": 3,
                "chunk_in_features": 512,
                "full_packet_bytes": full_packet,
                "chunk_packet_bytes": chunk_packet,
                "max_abs_diff": max(diffs),
                "mismatch_count": mismatch_count,
                "result": result,
            }
        )
        log.append(
            f"F_1536x16_{pattern.name}: {result} full_packet={full_packet} "
            f"chunk_packet={chunk_packet} full={full} chunked={chunked}"
        )
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="", encoding="ascii") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    log_path.write_text("\n".join(log) + "\n", encoding="ascii")


def generate_dma_width_options() -> None:
    out_path = ROOT / "reports/s05_6_3_dma_length_width_options.csv"
    fields = [
        "width_bits",
        "max_btt_bytes",
        "fits_576_rowgroup_10368",
        "fits_1536_rowgroup_27648",
        "fits_2560_rowgroup_46080",
        "simple_mode_change_required",
        "notes",
    ]
    rows = []
    for width in [14, 15, 16, 18]:
        max_btt = (1 << width) - 1
        rows.append(
            {
                "width_bits": width,
                "max_btt_bytes": max_btt,
                "fits_576_rowgroup_10368": str(10368 <= max_btt).lower(),
                "fits_1536_rowgroup_27648": str(27648 <= max_btt).lower(),
                "fits_2560_rowgroup_46080": str(46080 <= max_btt).lower(),
                "simple_mode_change_required": "false",
                "notes": "current" if width == 14 else ("minimum_for_135M" if width == 15 else "preferred"),
            }
        )
    with out_path.open("w", newline="", encoding="ascii") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_docs() -> None:
    dma = parse_dma_xci()
    strategy = ROOT / "docs/s05_6_3_dma_length_strategy.md"
    strategy.write_text(
        "\n".join(
            [
                "# S05.6.3 DMA Length Strategy",
                "",
                "## Frozen Baseline",
                "",
                f"- Candidate tag: `{CANDIDATE_TAG}`",
                f"- Candidate 10 bitstream/XSA/BOOT hashes are recorded in `logs/s05_6_3_baseline_freeze.txt`.",
                "- Candidate 10 artifacts must not be overwritten.",
                "- S06 full runtime remains blocked until S05.6.3 passes.",
                "",
                "## Problem",
                "",
                f"- Current AXI DMA simple length width is `{dma.get('C_SG_LENGTH_WIDTH', 'UNKNOWN')}` bits.",
                f"- Current max simple BTT is `{DMA_MAX_SIMPLE_BYTES}` bytes.",
                "- A 576-wide row-group packet is 10,368 bytes and fits.",
                "- A 1536-wide row-group packet is 27,648 bytes and does not fit.",
                "- Therefore `F_1536x16` and `down_proj` row-groups are immediate blockers.",
                "",
                "## Plan A: Software Input-Dimension Chunking",
                "",
                "- Keep Candidate 10 bitstream/BOOT/XSA unchanged.",
                "- Split 1536-wide GEMV row-groups into 3 input chunks of 512.",
                "- Each chunk packet is 9,216 bytes, below the 16,383-byte simple DMA limit.",
                "- The FPGA computes each chunk output for the same 16 output rows.",
                "- The CPU only sums the FPGA chunk `output_i32` vectors in an `int64` accumulator and clamps the final 16-row output to `int32`.",
                "- This is not CPU GEMV fallback; the CPU does not multiply input and weights.",
                "- 576-wide row-groups stay unchunked.",
                "- Future 360M 2560-wide row-groups would split into 5 chunks of 512.",
                "",
                "## Plan B: DMA Length Width Expansion",
                "",
                "- Audit only while Plan A is running; do not change Vivado unless Plan A hits the transition criteria.",
                "- Keep AXI DMA simple mode and `C_INCLUDE_SG=0`.",
                "- Preferred width is 16 bits, giving max BTT 65,535 bytes.",
                "- Width 16 covers current 135M 1536-wide row-groups and future 2560-wide row-groups.",
                "- Any implementation must export bitstream/XSA/BOOT under new names and preserve Candidate 10.",
                "",
                "## Decision Rule",
                "",
                "- If Plan A passes board and proxy gates, S06 may proceed with documented software chunking.",
                "- If Plan A times out or creates excessive runtime complexity, switch to Plan B implementation.",
                "- Do not mix Plan A software chunking and Plan B hardware width changes in the same debug attempt.",
                "",
            ]
        ),
        encoding="ascii",
    )

    audit = ROOT / "docs/s05_6_3_dma_length_width_audit.md"
    audit.write_text(
        "\n".join(
            [
                "# S05.6.3 DMA Length Width Audit",
                "",
                "## Current AXI DMA",
                "",
                f"- XCI: `{DMA_XCI.relative_to(ROOT)}`",
                f"- `C_SG_LENGTH_WIDTH`: `{dma.get('C_SG_LENGTH_WIDTH', 'UNKNOWN')}`",
                f"- `c_sg_length_width`: `{dma.get('c_sg_length_width', 'UNKNOWN')}`",
                f"- `C_INCLUDE_SG`: `{dma.get('C_INCLUDE_SG', 'UNKNOWN')}`",
                f"- `c_include_sg`: `{dma.get('c_include_sg', 'UNKNOWN')}`",
                f"- MM2S enabled: `{dma.get('c_include_mm2s', 'UNKNOWN')}`",
                f"- S2MM enabled: `{dma.get('c_include_s2mm', 'UNKNOWN')}`",
                f"- MM2S stream width: `{dma.get('C_M_AXIS_MM2S_TDATA_WIDTH', 'UNKNOWN')}` bits",
                "",
                "## Width Options",
                "",
                "- 14 bits: max BTT 16,383 bytes. Current setting. 576-wide row-groups fit; 1536-wide row-groups fail.",
                "- 15 bits: max BTT 32,767 bytes. Covers current 135M 1536-wide row-groups.",
                "- 16 bits: max BTT 65,535 bytes. Covers current 135M and documented future 2560-wide row-groups.",
                "- 18 bits: max BTT 262,143 bytes. Not needed for current row-group jobs.",
                "",
                "## Implementation Risk",
                "",
                "- Simple mode can remain enabled as long as `C_INCLUDE_SG` stays 0.",
                "- The BD/IP change is localized to AXI DMA configuration; address map and PS7 interfaces do not need deletion or regeneration.",
                "- Expected resource/timing impact is low because data width, clocks, and interconnect topology do not change.",
                "- A full regenerate/rebuild is still required to avoid stale AXI DMA output products.",
                "- Plan B implementation must use separate bitstream/XSA/BOOT names and rerun the full S05.5/S05.6 regression sequence.",
                "",
                "See `reports/s05_6_3_dma_length_width_options.csv` for the numeric table.",
                "",
            ]
        ),
        encoding="ascii",
    )


def main() -> int:
    generate_baseline()
    generate_packet_audit()
    generate_chunk_policy()
    generate_reference()
    generate_dma_width_options()
    write_docs()
    print("wrote S05.6.3 baseline/audit/reference artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
