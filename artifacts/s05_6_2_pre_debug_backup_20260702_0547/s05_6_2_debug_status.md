# S05.6.2 Debug Status

Created: 2026-07-02 KST

## Scope

S05.6.2 is limited to the deterministic 128-bit AXIS GEMV multi-block correctness failure for `blocks_per_row > 1`, starting with `B_64x16_P0`.

S06 remains blocked.

## Protected Baselines

- Active `GPTalk_dma.bit` / `GPTalk_dma.xsa` aliases are the 32-bit known-good artifacts, not the S05.5 128-bit baseline.
- S05.5 128-bit baseline remains protected under the `_s05_5_axis128_bram_scalar_74MHz` bitstream/XSA names and the matching `test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot` BOOT folder.
- S05.6.2 must not overwrite either protected baseline family.

## Current Source Classification

- Current `vivado_ip/rtl/gemv_q8_0_stream_core.v` is the `fixed_128_mac` snapshot plus the `mode1_emit_snapshot` delta.
- The `mode1_emit_snapshot` delta snapshots `block_acc[]` into `block_out[]` at `ST_BLOCK_DONE` and emits mode1 block outputs from that snapshot, instead of reading the live `block_acc[]` array during `ST_EMIT_BLOCK`.
- Current `vivado_ip/rtl/gemv_q8_0_dma_top.v` still matches the `fixed_128_mac` snapshot.
- Current source is not the board-tested `state_encoding_preserve_74MHz` source because the stream core differs from `artifacts/s05_6_1_prebuild_backup_20260701_2228_state_encoding_preserve/gemv_q8_0_stream_core.v`.
- Current source hash:
  - stream core: `fe0a0bc884b764fef1a00226ec2757ddcbdc6b509b5cd93a20e25730b751bd6a`
  - dma top: `eea8f8192ee62312b7ee8f25a87045f43e5c358554c2124a8c64c56bfdfb1dbf`

## Pre-Edit Snapshot

- `artifacts/s05_6_2_pre_wrapper_tb_snapshot_20260702_050523/`
- `artifacts/s05_6_2_prebuild_backup_20260702_052959_mode1_emit_snapshot/`

## Wrapper-Level Simulation

Added:

- `vivado_ip/tb/tb_gemv_q8_0_dma_top_multiblock.sv`
- `scripts/run_s05_6_2_dma_top_multiblock_sim.sh`

Intent:

- Instantiate `gemv_q8_0_dma_top`.
- Drive AXI-Lite configuration in the same order as `runtime_c/gemv_multiblock_test.c`.
- Run `B_64x16_P0` mode 0 and mode 1.
- Model input BRAM read latency at the wrapper boundary.
- Drive 128-bit MM2S-style AXIS input.
- Collect 32-bit S2MM-style AXIS output with deterministic TREADY backpressure.
- Compare all output lanes and highlight lane 4 and lane 12.
- Check `M_AXIS_TLAST`, output word count, status, error, and debug counters.

Result:

- `logs/s05_6_2_dma_top_multiblock_sim.txt`
- PASS
- current source hash in wrapper sim:
  - stream core: `fe0a0bc884b764fef1a00226ec2757ddcbdc6b509b5cd93a20e25730b751bd6a`
  - dma top: `eea8f8192ee62312b7ee8f25a87045f43e5c358554c2124a8c64c56bfdfb1dbf`
- mode0 lane4: got `29`, expected `29`
- mode0 lane12: got `-11`, expected `-11`
- mode1 lane4: block0 got `10`, expected `10`; block1 got `19`, expected `19`
- mode1 lane12: block0 got `0`, expected `0`; block1 got `-11`, expected `-11`
- status/error/debug counters normal in simulation

The wrapper-level TB does not reproduce the board failure.

Core-level `stream_core` multi-block RTL sim also passes with the same current stream-core source hash. `F_1536x16` remains skipped for the DMA length future blocker.

## Board Candidate 1

Candidate tag:

- `s05_6_2_fixed_128_mac_74MHz`

Build result:

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_fixed_128_mac_74MHz.bit`
- bitstream sha256: `b80a27672fe22e492e3966eada9d8f38c688fd6f7733cf4e8986e9e8b01b4eec`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_fixed_128_mac_74MHz.xsa`
- XSA sha256: `e72cea31ef00ead44a7caf114b4b15cf4bf0855a142b9a54500c9f45e4942c74`
- timing: PASS, setup WNS `0.466 ns`, hold WHS `0.015 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_fixed_128_mac_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `8c50935434e0ce9a57c5ee891ce45f9da839c4a8ad7e28e517aec6ed3fd06a70`
- FSBL: known-good S03
- U-Boot: known-good S03
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash

Board result:

- gate 1 S05 fake one-run: PASS
- gate 2 `B_64x16_P0` mode0 repeat 5: PASS
- gate 3 `B_64x16_P0` mode1 dump: FAIL, deterministic on repeat
- mode0 lane4: got `29`, expected `29`
- mode1 lane4:
  - block0 got `10`, expected `10`
  - block1 got `1043`, expected `19`
- mode1 lane12:
  - block0 got `1`, expected `0`
  - block1 got `-12`, expected `-11`
- logs:
  - `logs/s05_6_2_fixed_128_mac_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_fixed_128_mac_74mhz_mode0_B64P0_repeat5_board.txt`
  - `logs/s05_6_2_fixed_128_mac_74mhz_mode1_B64P0_dump_board.txt`
  - `logs/s05_6_2_fixed_128_mac_74mhz_mode1_B64P0_repeat5_board.txt`

Interpretation: `fixed_128_mac` fixes the original mode0 lane4 multi-block board failure, but mode1 direct block-output emission still corrupts lane4/lane12 on board.

## Board Candidate 2

Candidate tag:

- `s05_6_2_mode1_emit_snapshot_74MHz`

Intent:

- Single RTL delta from candidate 1: snapshot per-block accumulators before mode1 output emission.
- This is intended to isolate live `block_acc[]` readback during `ST_EMIT_BLOCK` from block reset/next-block activity in synthesized hardware.

Pre-board simulation:

- wrapper-level `dma_top` sim: PASS
- core-level `stream_core` multi-block sim: PASS

Build result:

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_snapshot_74MHz.bit`
- bitstream sha256: `1500f1d534f04fac4666b18270ffe13ed98fa5ecb88a14580243e0e1c8d4f372`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_snapshot_74MHz.xsa`
- XSA sha256: `839051d3299b482fde743ca61a2e1429bb50872df90bcb9398142551edae067c`
- timing: PASS, setup WNS `0.542 ns`, hold WHS `0.005 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_mode1_emit_snapshot_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `8f9f395222107734aad427fcce0425e96b4f80c153c03ff3ada69e8888ef23e7`
- FSBL: known-good S03, sha256 `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: known-good S03, sha256 `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash
- previous SD BOOT backup: `/dev/sdb1:/BOOT_BEFORE_TEST_S05_6_2_MODE1_EMIT_SNAPSHOT_74MHZ_S03_FSBL_S03_UBOOT_20260702_053843.BIN`
- previous SD BOOT backup sha256: `8c50935434e0ce9a57c5ee891ce45f9da839c4a8ad7e28e517aec6ed3fd06a70`

Board result: pending.

## Future Blocker: DMA Length

The current AXI DMA simple-mode length width is `C_SG_LENGTH_WIDTH=14`, so one simple transfer is limited to 16383 bytes.

This is not the cause of the current `B_64x16_P0` failure because that packet is 1152 bytes.

The length limit remains a real future blocker for `F_1536x16` and real `down_proj` / `lm_head`-class tensors. S05.6.2 must not change `C_SG_LENGTH_WIDTH`, switch to scatter-gather DMA, or use DMA-length changes to mask the `B_64x16_P0` correctness bug. After S05.6.2 passes, move this to S05.6.3 as a DMA length/chunking strategy task.
