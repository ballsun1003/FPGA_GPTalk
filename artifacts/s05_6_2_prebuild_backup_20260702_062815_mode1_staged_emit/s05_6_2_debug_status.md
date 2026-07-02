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

- Current source is the `fixed_128_mac` functional base plus the `mode1_staged_emit` delta.
- The rejected `mode1_emit_snapshot` functional delta is not present.
- The rejected intrusive 4-record/2048-bit narrow debug bus is not present.
- The rejected selected narrow debug RTL is not present.
- The `mode1_staged_emit` delta changes mode1 only: after each block completes, it stages `block_acc[emit_lane]` through the existing `scale_block_acc_reg` into `row_out[emit_lane]`, then emits mode1 data from `row_out[]`. This avoids the synthesized board-only failure seen when `ST_EMIT_BLOCK` emitted directly from `block_acc[]`.
- Current source is not the board-tested `state_encoding_preserve_74MHz` source because the stream core differs from `artifacts/s05_6_1_prebuild_backup_20260701_2228_state_encoding_preserve/gemv_q8_0_stream_core.v`.
- Current source hash:
  - stream core: `29dec5852909bfd59729fff1bae62b40cd9e57fa9ac80165448a794e7199ae54`
  - dma top: `eea8f8192ee62312b7ee8f25a87045f43e5c358554c2124a8c64c56bfdfb1dbf`
  - ctrl axi-lite: `5657ca43fe0283896160dc9ee3fb1fbbcabe5918869296267678a9711609e18e`
  - board C diagnostic: `7843f64fabd1ca392e47f45840ec0b455da48b930ce48678fdd3293806febe6b`

## Pre-Edit Snapshot

- `artifacts/s05_6_2_pre_wrapper_tb_snapshot_20260702_050523/`
- `artifacts/s05_6_2_prebuild_backup_20260702_052959_mode1_emit_snapshot/`
- `artifacts/s05_6_2_prebuild_backup_20260702_061137_selected_debug/`
- `artifacts/s05_6_2_prebuild_backup_20260702_062815_mode1_staged_emit/`

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
  - stream core: `29dec5852909bfd59729fff1bae62b40cd9e57fa9ac80165448a794e7199ae54`
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

Board result update:

- gate 1 S05 fake one-run: PASS
- gate 2 `B_64x16_P0` mode0 repeat 5: FAIL
- mode0 lane4: got `4194333`, expected `29`, repeat 5 deterministic
- mode0 lane12 in OUTVEC: got `-4194311`, expected `-11`
- gate stopped at step 2
- logs:
  - `logs/s05_6_2_mode1_emit_snapshot_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_mode1_emit_snapshot_74mhz_mode0_B64P0_repeat5_board.txt`
  - `reports/s05_6_2_mode1_emit_snapshot_74mhz_mode0_B64P0_repeat5_board.csv`

Interpretation: this candidate is rejected. The mode1 snapshot change made the synthesized board behavior worse and is not a valid fix.

## Intrusive Narrow Debug Candidate (Rejected)

Candidate tag:

- `s05_6_2_narrow_debug_74MHz`

Source basis:

- Reverted the rejected `mode1_emit_snapshot` functional delta.
- Based on `fixed_128_mac` behavior plus narrow debug-only latches.

Debug scope:

- only lane4/lane12
- only block0/block1
- `debug_probe_index`/`debug_probe_data` AXI-Lite window at byte offsets `0xa8`/`0xac`
- four records: `b0_l4`, `b0_l12`, `b1_l4`, `b1_l12`
- each record latches:
  - input index
  - input_i16 value
  - weight_i8 value
  - product
  - block_acc before
  - block_acc after
  - scale_q
  - scaled
  - row_acc before
  - row_acc after
  - output word
  - S2MM tdata
  - S2MM tkeep/tlast
  - block_idx/lane_idx
  - input beat_idx
  - output beat_idx

Current debug source hash:

- stream core: `2715feb04e85dc1e3359704227bd8cf8227511fa252271d426f4294c75fc5c83`
- dma top: `30d1195c7373c12fb04e87aa49e60c264a849ac56f451fa97ef6102956a4a921`
- ctrl axi-lite: `70e74e0bd58b866fc2ecc90a15c9c92b09b3588d7d36064da11fc6f27eb20642`
- board C diagnostic: `4509cb050ee4378cc1084c8cb0d144ef616cb018511eb5e3aa37c04887d16a08`

Pre-board simulation:

- wrapper-level `dma_top` sim: PASS
- core-level `stream_core` multi-block sim: PASS

Build result:

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_narrow_debug_74MHz.bit`
- bitstream sha256: `12082b8ab68d8dd7c37b4f427bd909470b23ecffdff3fbc2a52b1cc02beb13ad`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_narrow_debug_74MHz.xsa`
- XSA sha256: `a1f0dffb82fcb0747bcf7f1cfff549e078f208a487e3ca282a21a98bcc4feb1b`
- timing: PASS, setup WNS `0.914 ns`, hold WHS `0.010 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_narrow_debug_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `41c211cb2680d17ff1959bd92094f45b6fbbc70283b68d8795922b0a7774bccd`
- FSBL: known-good S03, sha256 `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: known-good S03, sha256 `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash
- previous SD BOOT backup: `/dev/sdb1:/BOOT_BEFORE_TEST_S05_6_2_NARROW_DEBUG_74MHZ_S03_FSBL_S03_UBOOT_20260702_060109.BIN`
- previous SD BOOT backup sha256: `8f9f395222107734aad427fcce0425e96b4f80c153c03ff3ada69e8888ef23e7`

Board result:

- gate 1 S05 fake one-run: FAIL
- mode0 fake output: got `-845568, 35840, -1975`; expected `-48, 19, -6`
- mode1 fake output: got `-3277824, 55264, -14896`; expected `-193, 38, -50`
- gate stopped at step 1
- logs:
  - `logs/s05_6_2_narrow_debug_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_narrow_debug_74mhz_s05_fake_one_run.txt`

Interpretation: this debug candidate is rejected because the instrumentation itself changes basic fake-GEMV behavior on board.

## Selected Narrow Debug Candidate (Rejected)

Candidate tag:

- `s05_6_2_selected_debug_74MHz`

Source basis:

- Reverted the rejected intrusive debug bus.
- Based on `fixed_128_mac` behavior plus one selected debug record exposed through AXI-Lite.
- Debug remains disabled by default, and only one of block0/lane4, block0/lane12, block1/lane4, or block1/lane12 is selected per run.

Current source hash:

- stream core: `0e4148bf9b7ac4bd330515d2e961051205b879e32159e5f0555423349e170a1c`
- dma top: `4d15afd594489e4d1ab47e456026443702765db1e754f6ac6b88fecbd95ea531`
- ctrl axi-lite: `462c9a1eb286383b44ecac16e910ca32c570442cc2fe299a7e7a94059c0d00c2`
- board C diagnostic: `7843f64fabd1ca392e47f45840ec0b455da48b930ce48678fdd3293806febe6b`

Pre-board simulation:

- wrapper-level `dma_top` sim: PASS
- core-level `stream_core` multi-block sim: PASS
- wrapper sim mode0 lane4: got `29`, expected `29`
- wrapper sim mode1 lane4/lane12:
  - block0 lane4 got `10`, expected `10`
  - block0 lane12 got `0`, expected `0`
  - block1 lane4 got `19`, expected `19`
  - block1 lane12 got `-11`, expected `-11`

Build result:

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_selected_debug_74MHz.bit`
- bitstream sha256: `387fca2d3ae07d172b01dfd45fa81d602d65cf35233b51fc736d8cec8419b7b3`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_selected_debug_74MHz.xsa`
- XSA sha256: `56f3a3b2c8593486e95754db71c80fceac61fcba7fd79df0990c2bd805869320`
- timing: PASS, setup WNS `0.569 ns`, hold WHS `0.009 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_selected_debug_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `166bd01560d288bef690b680b6c0079b479c5aff532d322908b01d7dd53f60f0`
- FSBL: known-good S03, sha256 `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: known-good S03, sha256 `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash
- previous SD BOOT backup: `/dev/sdb1:/BOOT_BEFORE_TEST_S05_6_2_SELECTED_DEBUG_74MHZ_S03_FSBL_S03_UBOOT_20260702_061854.BIN`
- previous SD BOOT backup sha256: `41c211cb2680d17ff1959bd92094f45b6fbbc70283b68d8795922b0a7774bccd`

Board result:

- gate 1 S05 fake one-run: FAIL
- mode0 fake output:
  - result0 got `-48`, expected `-48`
  - result1 got `172228627` (`0x0a440013`), expected `19`
  - result2 got `-50036736` (`0xfd048000`), expected `-6`
- mode1 fake output:
  - result0 got `-193`, expected `-193`
  - result1 got `33554470` (`0x02000026`), expected `38`
  - result2 got `-386400306` (`0xe8f7ffce`), expected `-50`
- gate stopped at step 1
- logs:
  - `logs/s05_6_2_selected_debug_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_selected_debug_74mhz_s05_fake_one_run.txt`

Interpretation: this debug candidate is rejected. Even with probe disabled by default, the added debug fanout/variable-index reads change synthesized board behavior.

## Board Candidate 4

Candidate tag:

- `s05_6_2_mode1_staged_emit_74MHz`

Intent:

- Single functional RTL delta from `fixed_128_mac`.
- Keep the mode0 board-PASS path unchanged.
- Replace direct mode1 output from `block_acc[emit_lane]` with a staged path:
  - `ST_BLOCK_STAGE_LOAD`: `scale_block_acc_reg <= block_acc[emit_lane]`
  - `ST_BLOCK_STAGE_STORE`: `row_out[emit_lane] <= scale_block_acc_reg`
  - `ST_EMIT_BLOCK`: emit `row_out[emit_lane]`
- This reuses the `block_acc[emit_lane]` load pattern already exercised by the passing mode0 scale path and avoids adding a new block-output array.

Current source hash:

- stream core: `29dec5852909bfd59729fff1bae62b40cd9e57fa9ac80165448a794e7199ae54`
- dma top: `eea8f8192ee62312b7ee8f25a87045f43e5c358554c2124a8c64c56bfdfb1dbf`
- ctrl axi-lite: `5657ca43fe0283896160dc9ee3fb1fbbcabe5918869296267678a9711609e18e`
- board C diagnostic: `7843f64fabd1ca392e47f45840ec0b455da48b930ce48678fdd3293806febe6b`

Pre-board simulation:

- wrapper-level `dma_top` sim: PASS
- core-level `stream_core` multi-block sim: PASS
- wrapper sim mode0 lane4: got `29`, expected `29`
- wrapper sim mode0 lane12: got `-11`, expected `-11`
- wrapper sim mode1 lane4/lane12:
  - block0 lane4 got `10`, expected `10`
  - block0 lane12 got `0`, expected `0`
  - block1 lane4 got `19`, expected `19`
  - block1 lane12 got `-11`, expected `-11`

Build result:

- pending

Board result:

- pending

## Future Blocker: DMA Length

The current AXI DMA simple-mode length width is `C_SG_LENGTH_WIDTH=14`, so one simple transfer is limited to 16383 bytes.

This is not the cause of the current `B_64x16_P0` failure because that packet is 1152 bytes.

The length limit remains a real future blocker for `F_1536x16` and real `down_proj` / `lm_head`-class tensors. S05.6.2 must not change `C_SG_LENGTH_WIDTH`, switch to scatter-gather DMA, or use DMA-length changes to mask the `B_64x16_P0` correctness bug. After S05.6.2 passes, move this to S05.6.3 as a DMA length/chunking strategy task.
