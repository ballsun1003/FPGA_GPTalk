#!/usr/bin/env python3
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "runtime_c" / "gemv_hw_test.c"
WORDS_LOG = ROOT / "logs" / "s05_packet_words.txt"
SUMMARY_LOG = ROOT / "logs" / "s05_packet_summary.txt"

LANES = 16
Q8_BLOCK_SIZE = 32
IN_FEATURES = 32
OUT_FEATURES = 3
PADDED_OUT_FEATURES = 16
BLOCKS_PER_ROW = 1
SCALE_BYTES = 64
WEIGHT_BYTES = 512
PACKET_BYTES = 576


def extract_array(source: str, name: str) -> bytes:
    pattern = rf"static const uint8_t {name}\[\] = \{{(.*?)\}};"
    match = re.search(pattern, source, re.S)
    if not match:
        raise SystemExit(f"array not found: {name}")
    values = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{2}", match.group(1))]
    return bytes(values)


def le32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little", signed=False)


def main() -> None:
    source = SRC.read_text(encoding="ascii")
    scale = extract_array(source, "EMBEDDED_SCALE_Q_I32")
    weight = extract_array(source, "EMBEDDED_WEIGHT_Q8_FPGA_LAYOUT")
    packet = scale + weight

    scale_words = SCALE_BYTES // 4
    weight_words = WEIGHT_BYTES // 4
    total_words = PACKET_BYTES // 4
    first_weight_word = scale_words
    expected_tlast_word = total_words - 1
    expected_tlast_weight_col = Q8_BLOCK_SIZE - 1
    expected_tlast_lane_base = LANES - 4

    checks = {
        "packet_bytes": len(packet),
        "scale_bytes": len(scale),
        "weight_bytes": len(weight),
        "total_words": total_words,
        "scale_words": scale_words,
        "weight_words": weight_words,
        "first_weight_word": first_weight_word,
        "last_word_index": expected_tlast_word,
        "expected_tlast_word": expected_tlast_word,
        "expected_tlast_weight_col": expected_tlast_weight_col,
        "expected_tlast_lane_base": expected_tlast_lane_base,
    }
    expected = {
        "packet_bytes": 576,
        "scale_bytes": 64,
        "weight_bytes": 512,
        "total_words": 144,
        "scale_words": 16,
        "weight_words": 128,
        "first_weight_word": 16,
        "last_word_index": 143,
        "expected_tlast_word": 143,
        "expected_tlast_weight_col": 31,
        "expected_tlast_lane_base": 12,
    }
    failures = [k for k, v in expected.items() if checks[k] != v]

    padded_scale_nonzero = []
    for lane in range(OUT_FEATURES, PADDED_OUT_FEATURES):
        value = le32(scale, lane * 4)
        if value != 0:
            padded_scale_nonzero.append((lane, value))

    padded_weight_nonzero = []
    for col in range(Q8_BLOCK_SIZE):
        for lane in range(OUT_FEATURES, PADDED_OUT_FEATURES):
            off = col * LANES + lane
            value = weight[off]
            if value != 0:
                padded_weight_nonzero.append((col, lane, value))

    WORDS_LOG.parent.mkdir(parents=True, exist_ok=True)
    with WORDS_LOG.open("w", encoding="ascii") as f:
        f.write("word_index byte_offset role scale_lane weight_col lane_base tdata_hex software_expected_tlast\n")
        for word_index in range(total_words):
            byte_offset = word_index * 4
            if word_index < scale_words:
                role = "scale"
                scale_lane = word_index
                weight_col = "-"
                lane_base = "-"
            else:
                role = "weight"
                rel = word_index - first_weight_word
                scale_lane = "-"
                weight_col = rel // (LANES // 4)
                lane_base = (rel % (LANES // 4)) * 4
            f.write(
                f"{word_index:03d} {byte_offset:04d} {role} {scale_lane} "
                f"{weight_col} {lane_base} 0x{le32(packet, byte_offset):08x} "
                f"{1 if word_index == expected_tlast_word else 0}\n"
            )

    with SUMMARY_LOG.open("w", encoding="ascii") as f:
        f.write("S05 packet summary\n")
        for key in [
            "packet_bytes",
            "scale_bytes",
            "weight_bytes",
            "total_words",
            "scale_words",
            "weight_words",
            "first_weight_word",
            "last_word_index",
            "expected_tlast_word",
            "expected_tlast_weight_col",
            "expected_tlast_lane_base",
        ]:
            f.write(f"{key}: {checks[key]}\n")
        f.write(f"dma_mm2s_len: {PACKET_BYTES}\n")
        f.write(f"software_expected_tlast_words: {expected_tlast_word}\n")
        f.write(f"padded_scale_nonzero_count: {len(padded_scale_nonzero)}\n")
        f.write(f"padded_weight_nonzero_count: {len(padded_weight_nonzero)}\n")
        f.write(f"contract_check: {'PASS' if not failures and not padded_scale_nonzero and not padded_weight_nonzero else 'FAIL'}\n")
        if failures:
            f.write("failed_fields: " + ",".join(failures) + "\n")
        if padded_scale_nonzero:
            f.write("padded_scale_nonzero: " + repr(padded_scale_nonzero) + "\n")
        if padded_weight_nonzero:
            f.write("padded_weight_nonzero: " + repr(padded_weight_nonzero) + "\n")

    print(f"wrote {WORDS_LOG}")
    print(f"wrote {SUMMARY_LOG}")


if __name__ == "__main__":
    main()
