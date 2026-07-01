#!/usr/bin/env python3
"""Build the S05.4 real SmolLM2 Q8_0 GEMV throughput model.

This is a PC-side model only. It reads the existing tensor map and S05.3
benchmark log, then writes the S05.4 CSV/report artifacts.
"""

from __future__ import annotations

import csv
import json
import math
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PROJECT_ROOT = Path(__file__).resolve().parents[1]
TENSOR_MAP_PATH = PROJECT_ROOT / "fpga_layout" / "tensor_map.json"
S05_3_BENCH_PATH = PROJECT_ROOT / "logs" / "s05_3_repeated_benchmark.txt"
CSV_PATH = PROJECT_ROOT / "reports" / "s05_4_real_workload_throughput_model.csv"
DOC_PATH = PROJECT_ROOT / "docs" / "s05_4_real_workload_throughput_model.md"
LOG_PATH = PROJECT_ROOT / "logs" / "s05_4_real_workload_throughput_model.txt"

LANES = 16
Q8_0_BLOCK = 32
SCALE_BYTES_PER_LANE = 4
WEIGHT_BYTES_PER_VALUE = 1
OUTPUT_BYTES_PER_VALUE = 4
AXIS32_BYTES = 4
AXIS128_BYTES = 16
FCLK_74_MHZ = 74.0
FCLK_50_MHZ = 50.0

ROLE_TO_TARGET = {
    "attention_q_projection": "q_proj",
    "attention_k_projection": "k_proj",
    "attention_v_projection": "v_proj",
    "attention_output_projection": "o_proj",
    "mlp_gate_projection": "gate_proj",
    "mlp_up_projection": "up_proj",
    "mlp_down_projection": "down_proj",
    "lm_head_tied_token_embedding": "lm_head",
}
TARGET_ORDER = {
    "q_proj": 0,
    "k_proj": 1,
    "v_proj": 2,
    "o_proj": 3,
    "gate_proj": 4,
    "up_proj": 5,
    "down_proj": 6,
    "lm_head": 7,
}


@dataclass(frozen=True)
class BenchOverhead:
    fixed_us: float
    reset_once_us: float | None
    reset_every_us: float | None


def parse_layer(name: str) -> int:
    match = re.search(r"blk\.(\d+)\.", name)
    if match:
        return int(match.group(1))
    match = re.search(r"layer_(\d+)_", name)
    if match:
        return int(match.group(1))
    return -1


def product(values: list[int]) -> int:
    result = 1
    for value in values:
        result *= value
    return result


def load_tensor_map() -> list[dict[str, Any]]:
    with TENSOR_MAP_PATH.open("r", encoding="utf-8") as file_obj:
        data = json.load(file_obj)
    mappings = data.get("mappings")
    if not isinstance(mappings, list):
        raise ValueError(f"{TENSOR_MAP_PATH} does not contain a mappings list")
    return mappings


def parse_s05_3_overhead() -> BenchOverhead:
    if not S05_3_BENCH_PATH.exists():
        return BenchOverhead(fixed_us=246.0, reset_once_us=None, reset_every_us=None)

    reset_once_values: list[float] = []
    reset_every_values: list[float] = []
    line_re = re.compile(
        r"poll_strategy=busy .* reset_strategy=(\S+) .* avg_us=([0-9]+(?:\.[0-9]+)?)"
    )
    for line in S05_3_BENCH_PATH.read_text(encoding="utf-8").splitlines():
        match = line_re.search(line)
        if not match:
            continue
        strategy = match.group(1)
        avg_us = float(match.group(2))
        if strategy == "reset_once_reuse":
            reset_once_values.append(avg_us)
        elif strategy == "reset_every_run":
            reset_every_values.append(avg_us)

    reset_once_us = max(reset_once_values) if reset_once_values else None
    reset_every_us = max(reset_every_values) if reset_every_values else None
    fixed_us = reset_every_us if reset_every_us is not None else 246.0
    return BenchOverhead(
        fixed_us=fixed_us,
        reset_once_us=reset_once_us,
        reset_every_us=reset_every_us,
    )


def model_tensor(mapping: dict[str, Any], overhead: BenchOverhead) -> dict[str, Any]:
    shape = mapping.get("shape")
    if not isinstance(shape, list) or len(shape) < 2:
        raise ValueError(f"invalid shape for {mapping.get('original_name')}: {shape}")
    if not all(isinstance(item, int) for item in shape):
        raise ValueError(f"non-integer shape for {mapping.get('original_name')}: {shape}")

    in_features = shape[0]
    out_features = product(shape[1:])
    if in_features % Q8_0_BLOCK != 0:
        raise ValueError(
            f"{mapping['original_name']} in_features={in_features} is not divisible "
            f"by Q8_0 block size {Q8_0_BLOCK}"
        )

    blocks_per_row = in_features // Q8_0_BLOCK
    padded_out_features = math.ceil(out_features / LANES) * LANES
    row_padding = padded_out_features - out_features
    row_groups = padded_out_features // LANES

    scale_bytes = row_groups * blocks_per_row * LANES * SCALE_BYTES_PER_LANE
    weight_bytes = (
        row_groups
        * blocks_per_row
        * Q8_0_BLOCK
        * LANES
        * WEIGHT_BYTES_PER_VALUE
    )
    total_packet_bytes = scale_bytes + weight_bytes
    output_bytes = out_features * OUTPUT_BYTES_PER_VALUE
    mac_count = in_features * out_features

    scale_beats_32 = scale_bytes // AXIS32_BYTES
    weight_beats_32 = weight_bytes // AXIS32_BYTES
    total_beats_32 = scale_beats_32 + weight_beats_32

    scale_beats_128 = math.ceil(scale_bytes / AXIS128_BYTES)
    weight_beats_128 = math.ceil(weight_bytes / AXIS128_BYTES)
    total_beats_128 = scale_beats_128 + weight_beats_128

    stream_us_32_74 = total_beats_32 / FCLK_74_MHZ
    stream_us_128_74 = total_beats_128 / FCLK_74_MHZ
    stream_us_128_50 = total_beats_128 / FCLK_50_MHZ
    speedup = total_beats_32 / total_beats_128 if total_beats_128 else 0.0

    row_group_mac_cycles_16lane = row_groups * blocks_per_row * Q8_0_BLOCK
    lane_util = out_features / padded_out_features if padded_out_features else 0.0
    fixed = overhead.fixed_us

    row: dict[str, Any] = {
        "tensor_name": mapping["original_name"],
        "internal_name": mapping["internal_name"],
        "source_role": mapping.get("source_role", mapping["role"]),
        "role": mapping["role"],
        "target": ROLE_TO_TARGET[mapping["role"]],
        "model_note": mapping.get("model_note", ""),
        "layer": parse_layer(mapping["original_name"]),
        "dtype_or_quant_type": mapping.get("dtype_or_quant_type", ""),
        "gguf_shape": json.dumps(shape, separators=(",", ":")),
        "gguf_nbytes": mapping.get("nbytes", ""),
        "in_features": in_features,
        "out_features": out_features,
        "q8_0_blocks_per_row": blocks_per_row,
        "lanes": LANES,
        "padded_out_features": padded_out_features,
        "row_padding": row_padding,
        "row_groups": row_groups,
        "scale_bytes": scale_bytes,
        "weight_bytes": weight_bytes,
        "total_packet_bytes": total_packet_bytes,
        "output_bytes": output_bytes,
        "mac_count": mac_count,
        "scale_beats_32": scale_beats_32,
        "weight_beats_32": weight_beats_32,
        "total_beats_32": total_beats_32,
        "lane_utilization_estimate": lane_util,
        "stream_cycles_32_74mhz": total_beats_32,
        "stream_us_32_74mhz": stream_us_32_74,
        "scale_beats_128": scale_beats_128,
        "weight_beats_128": weight_beats_128,
        "total_beats_128": total_beats_128,
        "stream_cycles_128_74mhz": total_beats_128,
        "stream_us_128_74mhz": stream_us_128_74,
        "stream_cycles_128_50mhz": total_beats_128,
        "stream_us_128_50mhz": stream_us_128_50,
        "speedup_128_vs_32": speedup,
        "mac_cycles_16lane_estimate": row_group_mac_cycles_16lane,
        "mac_us_16lane_74mhz_estimate": row_group_mac_cycles_16lane / FCLK_74_MHZ,
        "pure_stream_compute_us_32_74mhz": max(
            stream_us_32_74, row_group_mac_cycles_16lane / FCLK_74_MHZ
        ),
        "pure_stream_compute_us_128_74mhz": max(
            stream_us_128_74, row_group_mac_cycles_16lane / FCLK_74_MHZ
        ),
    }

    for batch in (1, 8, 16, 64):
        suffix = "single" if batch == 1 else f"batch{batch}"
        amortized = fixed / batch
        row[f"{suffix}_fixed_overhead_us"] = amortized
        row[f"{suffix}_total_us_32_74mhz"] = (
            row["pure_stream_compute_us_32_74mhz"] + amortized
        )
        row[f"{suffix}_total_us_128_74mhz"] = (
            row["pure_stream_compute_us_128_74mhz"] + amortized
        )

    return row


def select_target_tensors(mappings: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], list[str]]:
    selected: list[dict[str, Any]] = []
    for mapping in mappings:
        if not mapping.get("used", False):
            continue
        if mapping.get("dtype_or_quant_type") != "Q8_0":
            continue
        if mapping.get("role") in ROLE_TO_TARGET:
            selected.append(mapping)

    notes: list[str] = []
    names = {item.get("original_name") for item in mappings}
    internals = {item.get("internal_name") for item in mappings}
    if "output.weight" not in names and "lm_head" not in internals:
        token_embedding = next(
            (
                item
                for item in mappings
                if item.get("original_name") == "token_embd.weight"
                and item.get("dtype_or_quant_type") == "Q8_0"
            ),
            None,
        )
        if token_embedding is None:
            notes.append(
                "lm_head/output.weight is not present and token_embd.weight was not found; "
                "lm_head is not modeled."
            )
        else:
            tied_lm_head = dict(token_embedding)
            tied_lm_head["source_role"] = token_embedding.get("role", "")
            tied_lm_head["role"] = "lm_head_tied_token_embedding"
            tied_lm_head["internal_name"] = "lm_head_tied_tok_embeddings"
            tied_lm_head["model_note"] = (
                "No separate output.weight in this GGUF; modeled token_embd.weight as "
                "tied lm_head. See pycharm/golden/lm_head_slice/manifest.json."
            )
            selected.append(tied_lm_head)
            notes.append(
                "No separate output.weight is present. token_embd.weight is modeled as "
                "the tied lm_head because pycharm/golden/lm_head_slice/manifest.json "
                "states that the lm_head slice is generated from token_embd.weight."
            )

    return selected, notes


def write_csv(rows: list[dict[str, Any]]) -> None:
    CSV_PATH.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys())
    with CSV_PATH.open("w", encoding="utf-8", newline="") as file_obj:
        writer = csv.DictWriter(file_obj, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            formatted = {}
            for key, value in row.items():
                if isinstance(value, float):
                    formatted[key] = f"{value:.6f}"
                else:
                    formatted[key] = value
            writer.writerow(formatted)


def markdown_table(headers: list[str], rows: list[list[Any]]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(str(item) for item in row) + " |")
    return "\n".join(lines)


def write_doc(rows: list[dict[str, Any]], notes: list[str], overhead: BenchOverhead) -> None:
    by_target_shape: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    by_target: Counter[str] = Counter()
    for row in rows:
        key = (
            row["target"],
            row["in_features"],
            row["out_features"],
            row["q8_0_blocks_per_row"],
            row["row_groups"],
            row["total_packet_bytes"],
            row["total_beats_32"],
            row["total_beats_128"],
        )
        by_target_shape[key].append(row)
        by_target[row["target"]] += 1

    summary_rows = []
    for key, group in sorted(
        by_target_shape.items(), key=lambda item: (TARGET_ORDER[item[0][0]], item[0][1:])
    ):
        (
            target,
            in_features,
            out_features,
            blocks,
            row_groups,
            packet_bytes,
            beats32,
            beats128,
        ) = key
        sample = group[0]
        summary_rows.append(
            [
                target,
                len(group),
                f"{in_features}x{out_features}",
                blocks,
                row_groups,
                f"{packet_bytes:,}",
                f"{beats32:,}",
                f"{sample['stream_us_32_74mhz']:.3f}",
                f"{beats128:,}",
                f"{sample['stream_us_128_74mhz']:.3f}",
                f"{sample['single_total_us_32_74mhz']:.3f}",
                f"{sample['batch64_total_us_128_74mhz']:.3f}",
            ]
        )

    worst = sorted(rows, key=lambda row: row["total_packet_bytes"], reverse=True)[:8]
    worst_rows = [
        [
            row["tensor_name"],
            row["target"],
            f"{row['in_features']}x{row['out_features']}",
            f"{row['total_packet_bytes']:,}",
            f"{row['mac_count']:,}",
            f"{row['stream_us_32_74mhz']:.3f}",
            f"{row['stream_us_128_74mhz']:.3f}",
        ]
        for row in worst
    ]

    total_packet = sum(int(row["total_packet_bytes"]) for row in rows)
    total_beats32 = sum(int(row["total_beats_32"]) for row in rows)
    total_beats128 = sum(int(row["total_beats_128"]) for row in rows)
    total_mac = sum(int(row["mac_count"]) for row in rows)
    total_stream32_us = total_beats32 / FCLK_74_MHZ
    total_stream128_us = total_beats128 / FCLK_74_MHZ
    total_overhead_single = len(rows) * overhead.fixed_us
    total_overhead_batch64 = len(rows) * overhead.fixed_us / 64.0

    fake_packet_bytes = 576
    largest_packet = max(int(row["total_packet_bytes"]) for row in rows)
    largest_vs_fake = largest_packet / fake_packet_bytes

    notes_text = "\n".join(f"- {item}" for item in notes) if notes else "- none"
    target_counts = ", ".join(f"{target}={by_target[target]}" for target in sorted(by_target, key=TARGET_ORDER.get))
    ffn_worst = next(
        (
            row
            for row in sorted(rows, key=lambda item: item["total_packet_bytes"], reverse=True)
            if row["target"] in {"gate_proj", "up_proj", "down_proj"}
        ),
        None,
    )
    if worst[0]["target"] == "lm_head":
        if ffn_worst is None:
            bottleneck_text = (
                "The primary bottleneck is tied `lm_head` from `token_embd.weight`."
            )
        else:
            bottleneck_text = (
                f"The primary bottleneck is tied `lm_head` from `token_embd.weight`, "
                f"with {worst[0]['total_packet_bytes']:,} packet bytes and "
                f"{worst[0]['mac_count']:,} MACs. The largest transformer-block "
                f"projection classes remain `gate_proj`, `up_proj`, and `down_proj`, "
                f"each with {ffn_worst['total_packet_bytes']:,} packet bytes and "
                f"{ffn_worst['mac_count']:,} MACs per tensor."
            )
    else:
        bottleneck_text = (
            f"The bottleneck tensor classes are `gate_proj`, `up_proj`, and `down_proj`. "
            f"Each has {worst[0]['total_packet_bytes']:,} packet bytes and "
            f"{worst[0]['mac_count']:,} MACs per tensor in this model."
        )

    doc = f"""# S05.4 Real Workload Throughput Model

## Scope

This is a PC-side throughput model for the real SmolLM2-135M Q8_0 Linear/GEMV tensors. It does not integrate S06 runtime, create a new bitstream, or overwrite the active known-good BOOT/bitstream.

Input metadata:
- Tensor map: `{TENSOR_MAP_PATH.relative_to(PROJECT_ROOT)}`
- S05.3 overhead log: `{S05_3_BENCH_PATH.relative_to(PROJECT_ROOT)}`
- CSV output: `{CSV_PATH.relative_to(PROJECT_ROOT)}`

Model constants:
- lanes: {LANES}
- Q8_0 block size: {Q8_0_BLOCK}
- scale in FPGA packet: {SCALE_BYTES_PER_LANE} bytes per lane
- output element: {OUTPUT_BYTES_PER_VALUE} bytes
- 32-bit AXIS beat: {AXIS32_BYTES} bytes
- 128-bit AXIS beat: {AXIS128_BYTES} bytes
- stream clock model: 74 MHz, plus 128-bit comparison at 50 MHz
- fixed S05.3 quiet hot path overhead used in model: {overhead.fixed_us:.3f} us
- reset_once_reuse overhead observed: {overhead.reset_once_us if overhead.reset_once_us is not None else 'unavailable'} us
- reset_every_run overhead observed: {overhead.reset_every_us if overhead.reset_every_us is not None else 'unavailable'} us

## Tensor Coverage

- modeled tensors: {len(rows)}
- target counts: {target_counts}
- total packet bytes for one full pass over modeled GEMV tensors: {total_packet:,}
- total MACs for one full pass over modeled GEMV tensors: {total_mac:,}
- special target handling:
{notes_text}

The tied `lm_head` row uses actual `token_embd.weight` tensor metadata from `tensor_map.json`; it is not a fabricated weight file.

## Per-Shape Summary

{markdown_table([
        "target",
        "count",
        "in x out",
        "blocks/row",
        "row_groups",
        "packet bytes",
        "32b beats",
        "32b stream us @74",
        "128b beats",
        "128b stream us @74",
        "single 32b us",
        "batch64 128b us",
    ], summary_rows)}

## Largest Tensors

{markdown_table([
        "tensor",
        "target",
        "in x out",
        "packet bytes",
        "MACs",
        "32b stream us @74",
        "128b stream us @74",
    ], worst_rows)}

## Aggregate Estimates

- 32-bit AXIS pure stream time at 74 MHz: {total_stream32_us:.3f} us
- 128-bit AXIS pure stream time at 74 MHz: {total_stream128_us:.3f} us
- 128-bit AXIS theoretical stream speedup: {(total_beats32 / total_beats128):.3f}x
- single-GEMV fixed overhead if paid per tensor: {total_overhead_single:.3f} us
- batch64 amortized fixed overhead over modeled tensors: {total_overhead_batch64:.3f} us

The fixed overhead dominates the small and medium tensors if every GEMV is launched as an independent job. The tied `lm_head` is bandwidth dominated even with 128-bit AXIS. Batching is therefore a throughput requirement, not a cosmetic optimization.

## Conclusions

- 32-bit AXIS is enough for S06 correctness bring-up, but it is not enough as the final performance path. It feeds the lane16 packet as four-byte beats, so the stream beat count is 4x the 128-bit model.
- 128-bit AXIS is needed for the performance gate before judging the FPGA backend against CPU runtime. The model gives an exact 4.000x stream beat reduction for the current packet layout.
- Batching is required. S05.3 quiet hot path overhead is about {overhead.fixed_us:.0f} us per independent GEMV. It is large versus small projection stream time and still material for per-tensor launch overhead.
- {bottleneck_text}
- fake_gemv timing is not final performance. The fake packet is {fake_packet_bytes} bytes, while the largest real GEMV packet is {largest_packet:,} bytes, about {largest_vs_fake:.1f}x larger. S05 fake_gemv proves protocol/correctness, not end-to-end model throughput.

## S06 Gate

S06 may use the current 32-bit AXIS path only as a functional integration path. Before treating S06 performance as meaningful, the project needs a batching plan and a 128-bit AXIS data path model or implementation plan. S05.5 and S06 were not started by this S05.4 task.
"""
    DOC_PATH.write_text(doc, encoding="utf-8")


def main() -> int:
    mappings = load_tensor_map()
    selected, missing = select_target_tensors(mappings)
    if not selected:
        raise RuntimeError("no Q8_0 Linear/GEMV target tensors found")

    overhead = parse_s05_3_overhead()
    rows = [model_tensor(mapping, overhead) for mapping in selected]
    rows.sort(
        key=lambda row: (
            row["layer"] if row["layer"] >= 0 else 10_000,
            TARGET_ORDER[row["target"]],
            row["tensor_name"],
        )
    )

    write_csv(rows)
    write_doc(rows, missing, overhead)

    total_packet = sum(int(row["total_packet_bytes"]) for row in rows)
    total_beats32 = sum(int(row["total_beats_32"]) for row in rows)
    total_beats128 = sum(int(row["total_beats_128"]) for row in rows)
    largest = max(rows, key=lambda row: row["total_packet_bytes"])
    log = "\n".join(
        [
            "S05.4 real workload throughput model PASS",
            f"modeled_tensors={len(rows)}",
            f"fixed_overhead_us={overhead.fixed_us:.3f}",
            f"total_packet_bytes={total_packet}",
            f"total_beats_32={total_beats32}",
            f"total_beats_128={total_beats128}",
            f"theoretical_128_vs_32_speedup={total_beats32 / total_beats128:.6f}",
            f"largest_tensor={largest['tensor_name']}",
            f"largest_target={largest['target']}",
            f"largest_packet_bytes={largest['total_packet_bytes']}",
            f"csv={CSV_PATH.relative_to(PROJECT_ROOT)}",
            f"doc={DOC_PATH.relative_to(PROJECT_ROOT)}",
            "s05_5_started=no",
            "s06_started=no",
        ]
    )
    LOG_PATH.write_text(log + "\n", encoding="utf-8")
    print(log)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
