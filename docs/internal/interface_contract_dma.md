# DMA GEMV Interface Contract

Date: 2026-06-29 KST

This is the active hardware/software contract for the DMA GEMV path.

## Current DDR HP Port Policy

Bulk GEMV movement must use a PS high-performance DDR port. It does not have to
be HP0 specifically when another active path already uses HP0. In the current
HDMI+DMA hardware, video VDMA uses PS `S_AXI_HP0` and GEMV AXI DMA uses PS
`S_AXI_HP1`.

## Address Regions

| Region | Current HDMI+DMA base | Range | Access |
| --- | ---: | ---: | --- |
| GEMV control | `0x43CA0000` | `4K` | 32-bit AXI-Lite |
| AXI DMA | `0x40400000` | `64K` | Xilinx AXI DMA register map |
| Input BRAM | `0x42000000` | `64K` | 32-bit memory window |
| HDMI VDMA | `0x43010000` | `64K` | Xilinx AXI VDMA register map |
| HDMI VTC | `0x43C10000` | `64K` | Xilinx VTC register map |
| HDMI dynclk | `0x43C20000` | `64K` | AXI dynclk register map |

Vivado is the source of truth after address assignment. The generated address
report for the current HDMI+DMA hardware is `logs/hw_dma_hdmi_address_map.txt`.

## GEMV Control Register Map

Offsets are relative to the GEMV control base.

| Offset | Name | Access | Description |
| ---: | --- | --- | --- |
| `0x00` | `VERSION` | RO | DMA GEMV wrapper version, currently `0x000A0001` |
| `0x04` | `CONTROL` | WO | bit0 start pulse, bit1 clear sticky status and pulse core reset |
| `0x08` | `STATUS` | RO | bit0 busy, bit1 done sticky, bit2 core error, bit3 stream input ready, bit4 result output valid, bit5 result backpressure, bit6 start while busy, bit7 mode |
| `0x0C` | `ERROR_CODE` | RO | core error code |
| `0x10` | `MODE` | RW | `0` scaled row output, `1` block accumulator debug output |
| `0x14` | `SCALE_SHIFT` | RW | fixed-scale right shift, default `20` |
| `0x18` | `IN_FEATURES` | RW | input feature count, multiple of 32 |
| `0x1C` | `OUT_FEATURES` | RW | valid output row count |
| `0x20` | `INPUT_BRAM_BASE` | RW | software-visible input BRAM base metadata |
| `0x24` | `WEIGHT_STREAM_LENGTH` | RW | expected MM2S byte count metadata |
| `0x28` | `RESULT_LENGTH` | RW | expected S2MM byte count metadata |
| `0x2C` | `START` | WO | bit0 alternate start pulse |
| `0x30` | `DONE` | RW | bit0 done sticky, bit1 raw done pulse; write bit0 to clear sticky |
| `0x34` | `DEBUG_ROW` | RO | current/debug row |
| `0x38` | `DEBUG_BLOCK` | RO | current/debug Q8_0 block |
| `0x3C` | `DEBUG_LANE` | RO | current/debug lane |
| `0x40` | `DEBUG_OUT0` | RO | S05 debug readback: latest emitted row 0 value |
| `0x44` | `DEBUG_OUT1` | RO | S05 debug readback: latest emitted row 1 value |
| `0x48` | `DEBUG_OUT2` | RO | S05 debug readback: latest emitted row 2 value |
| `0x4C` | `DEBUG_IN_COUNT` | RO | accepted S_AXIS input word count |
| `0x50` | `DEBUG_TLAST_COUNT` | RO | accepted word count when input TLAST was observed |
| `0x54` | `DEBUG_TLAST_TDATA` | RO | input TDATA observed with TLAST |
| `0x58` | `DEBUG_TLAST_TKEEP` | RO | input TKEEP observed with TLAST |
| `0x5C` | `DEBUG_SCALE0` | RO | mode=0 scale debug for row/lane 0 |
| `0x60` | `DEBUG_SCALE1` | RO | mode=0 scale debug for row/lane 1 |
| `0x64` | `DEBUG_SCALE2` | RO | mode=0 scale debug for row/lane 2 |
| `0x68` | `DEBUG_BLOCK0` | RO | mode=0 block accumulator debug for row/lane 0 |
| `0x6C` | `DEBUG_BLOCK1` | RO | mode=0 block accumulator debug for row/lane 1 |
| `0x70` | `DEBUG_BLOCK2` | RO | mode=0 block accumulator debug for row/lane 2 |
| `0x74` | `DEBUG_PRODUCT0_LO` | RO | low 32 bits of `block0 * scale0` |
| `0x78` | `DEBUG_PRODUCT0_HI` | RO | high 32 bits of `block0 * scale0` |
| `0x7C` | `DEBUG_PRODUCT1_LO` | RO | low 32 bits of `block1 * scale1` |
| `0x80` | `DEBUG_PRODUCT1_HI` | RO | high 32 bits of `block1 * scale1` |
| `0x84` | `DEBUG_PRODUCT2_LO` | RO | low 32 bits of `block2 * scale2` |
| `0x88` | `DEBUG_PRODUCT2_HI` | RO | high 32 bits of `block2 * scale2` |
| `0x8C` | `DEBUG_SCALED0` | RO | rounded/shifted mode=0 contribution for row/lane 0 |
| `0x90` | `DEBUG_SCALED1` | RO | rounded/shifted mode=0 contribution for row/lane 1 |
| `0x94` | `DEBUG_SCALED2` | RO | rounded/shifted mode=0 contribution for row/lane 2 |
| `0x98` | `DEBUG_ROW_ACC0` | RO | row accumulator after adding scaled row/lane 0 contribution |
| `0x9C` | `DEBUG_ROW_ACC1` | RO | row accumulator after adding scaled row/lane 1 contribution |
| `0xA0` | `DEBUG_ROW_ACC2` | RO | row accumulator after adding scaled row/lane 2 contribution |

AXI-Lite carries no bulk tensor payload in this contract.

## Software Sequence

1. Write the signed int16 input vector into the input BRAM memory window.
2. Prepare a packed scale/weight stream in a DMA-coherent DDR buffer.
3. Prepare a DMA-coherent DDR result buffer.
4. Program GEMV `MODE`, `SCALE_SHIFT`, `IN_FEATURES`, `OUT_FEATURES`,
   `WEIGHT_STREAM_LENGTH`, and `RESULT_LENGTH`.
5. Program AXI DMA S2MM with the result buffer address and byte count.
6. Program AXI DMA MM2S with the packed stream address and byte count.
7. Write GEMV `START`.
8. Poll DMA completion and GEMV `DONE`.
9. Compare the DDR result buffer against the relevant golden output.

## Input BRAM Packing

The input BRAM is 32-bit wide. Software packs two signed int16 input samples per
word:

```text
word[n][15:0]  = input[2*n]
word[n][31:16] = input[2*n + 1]
```

The GEMV top presents byte addresses to BRAM port B and selects the correct
half word internally.

## Stream Contract

The AXI stream into GEMV is one 32-bit word wide with `TKEEP=0xF`. The stream
order remains the proven core order:

```text
for each row group of 16 rows:
  for each Q8_0 block:
    16 signed int32 scale words, lane order 0..15
    for each of 32 input columns:
      four signed int8 weights packed little-endian per 32-bit word
```

`TLAST` marks the final word of the entire GEMV stream. The core raises an error
if the final marker appears early or late.

The stream core must accept an input beat only on the AXI Stream handshake:

```text
input_beat_fire = S_AXIS_TVALID && S_AXIS_TREADY
```

`TVALID` alone is not sufficient. Consuming on `TVALID` without `TREADY` can
desynchronize the core's internal word position from AXI DMA/FIFO handshakes and
produce false `ERR_TLAST`.

The output stream is one signed int32 value per 32-bit word with `TKEEP=0xF`.
`TLAST` marks the final result word for the configured run.

## S05 fake_gemv Exact Packet

For the active S05 fake_gemv smoke case:

| Field | Value |
| --- | ---: |
| lanes | `16` |
| in_features | `32` |
| out_features | `3` |
| padded_out_features | `16` |
| blocks_per_row | `1` |
| scale_bytes | `64` |
| weight_bytes | `512` |
| packet_bytes | `576` |
| total_words | `144` |
| scale_words | `16` |
| weight_words | `128` |
| expected input TLAST word | `143` |
| expected input TLAST byte offset | `572` |
| expected input TLAST location | `weight_col=31`, `lane_base=12` |

Padded lanes `3..15` must have zero scale and zero weights in this smoke case.
`debug_lane=8` on `ERR_TLAST` means the stream core detected the mismatch while
accepting the weight beat for lanes `8..11`; it does not mean the accelerator
was configured as 8-lane.

`WEIGHT_STREAM_LENGTH` is currently control/status metadata. It is written by
software and visible through the AXI-Lite control block, but
`gemv_q8_0_stream_core.v` computes its expected input TLAST position from
`in_features`, `out_features`, `LANES`, and `Q8_BLOCK_SIZE`; it does not use
`WEIGHT_STREAM_LENGTH` as an internal stream counter.
