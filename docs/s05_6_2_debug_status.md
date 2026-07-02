# S05.6.2 Debug Status

Created: 2026-07-02 KST

## Scope

S05.6.2 is limited to the deterministic 128-bit AXIS GEMV multi-block correctness failure for `blocks_per_row > 1`, starting with `B_64x16_P0`.

S05.6.2 correctness gate is PASS with Candidate 10. S06 was not started; the next step is S05.6.3 for DMA length/chunking before any larger real-tensor path work.

## Protected Baselines

- Active `GPTalk_dma.bit` / `GPTalk_dma.xsa` aliases are the 32-bit known-good artifacts, not the S05.5 128-bit baseline.
- S05.5 128-bit baseline remains protected under the `_s05_5_axis128_bram_scalar_74MHz` bitstream/XSA names and the matching `test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot` BOOT folder.
- S05.6.2 must not overwrite either protected baseline family.

## Current Source Classification

- Current source is Board Candidate 10, `s05_6_2_mode1_isolated_identity_scale_74MHz`.
- It keeps the Candidate 7 `dma_top_output_register` wrapper delta.
- It keeps the mode0 scale path structurally identical to Candidate 7.
- It adds mode1-only identity-scale states so mode1 emits the raw block accumulator through the staged scale/output path without using the rejected common mode mux.
- The rejected `mode1_emit_snapshot` functional delta is not present.
- The rejected intrusive 4-record/2048-bit narrow debug bus is not present.
- The rejected selected narrow debug RTL is not present.
- The rejected core-internal mode1 snapshot/debug deltas are not present.
- Current source is not the board-tested `state_encoding_preserve_74MHz` source because the stream core differs from `artifacts/s05_6_1_prebuild_backup_20260701_2228_state_encoding_preserve/gemv_q8_0_stream_core.v`.
- Current source hash:
  - stream core: `c2bb16ff5b035fcef2831108147009073ee26beaa353e64f25f4ad49a05d3e58`
  - dma top: `f50e9caca4959386cf1a330238630077d6a97c74d37d10cd442a5fd6851f859a`
  - ctrl axi-lite: `5657ca43fe0283896160dc9ee3fb1fbbcabe5918869296267678a9711609e18e`
  - board C diagnostic: `7843f64fabd1ca392e47f45840ec0b455da48b930ce48678fdd3293806febe6b`
  - batch/proxy harness: `1aed568cdb1e230c9832d6e6aca821ac04dd3e227bcca58f270e54d752b3b81e`

## Pre-Edit Snapshot

- `artifacts/s05_6_2_pre_wrapper_tb_snapshot_20260702_050523/`
- `artifacts/s05_6_2_prebuild_backup_20260702_052959_mode1_emit_snapshot/`
- `artifacts/s05_6_2_prebuild_backup_20260702_061137_selected_debug/`
- `artifacts/s05_6_2_prebuild_backup_20260702_062815_mode1_staged_emit/`
- `artifacts/s05_6_2_prebuild_backup_20260702_064307_mode1_final_apply_snapshot/`
- `artifacts/s05_6_2_prebuild_backup_20260702_065605_mode1_scalar_snapshot/`
- `artifacts/s05_6_2_prebuild_backup_20260702_070911_dma_top_output_register/`
- `artifacts/s05_6_2_prebuild_backup_20260702_0756_mode1_isolated_identity_scale/`

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
  - stream core: `c2bb16ff5b035fcef2831108147009073ee26beaa353e64f25f4ad49a05d3e58`
  - dma top: `f50e9caca4959386cf1a330238630077d6a97c74d37d10cd442a5fd6851f859a`
- mode0 lane4: got `29`, expected `29`
- mode0 lane12: got `-11`, expected `-11`
- mode1 lane4: block0 got `10`, expected `10`; block1 got `19`, expected `19`
- mode1 lane12: block0 got `0`, expected `0`; block1 got `-11`, expected `-11`
- status/error/debug counters normal in simulation

The final wrapper-level TB passes with the fixed Candidate 10 source.

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

Board result:

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

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_staged_emit_74MHz.bit`
- bitstream sha256: `2c8b9c68c1c3f34ac78ec053e39c857dc837b9186f6df48e87a0e7ae7067ad8c`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_staged_emit_74MHz.xsa`
- XSA sha256: `0eb5a0d1be78cb1105e2bcb5d260862f86de1c56c1760ecbb1ddf3db4b1422f3`
- timing: PASS, setup WNS `0.794 ns`, hold WHS `0.018 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_mode1_staged_emit_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `30131d75e893dd144fef7081125fcbd2f7944ee07733e6f9ee32230ea888ac95`
- FSBL: known-good S03, sha256 `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: known-good S03, sha256 `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash
- previous SD BOOT backup: `/dev/sdb1:/BOOT_BEFORE_TEST_S05_6_2_MODE1_STAGED_EMIT_74MHZ_S03_FSBL_S03_UBOOT_20260702_063519.BIN`
- previous SD BOOT backup sha256: `166bd01560d288bef690b680b6c0079b479c5aff532d322908b01d7dd53f60f0`

Board result:

- gate 1 S05 fake one-run: PASS
- gate 2 `B_64x16_P0` mode0 repeat 5: PASS
- gate 3 `B_64x16_P0` mode1 dump: FAIL
- repeat 5 mode1 diagnostic: deterministic FAIL
- mode0 lane4: got `29`, expected `29`
- mode1 lane4/lane12: PASS
  - block0 lane4 got `10`, expected `10`
  - block0 lane12 got `0`, expected `0`
  - block1 lane4 got `19`, expected `19`
  - block1 lane12 got `-11`, expected `-11`
- remaining mode1 failure:
  - block0 lane8 got `-1061`, expected `-37`, repeat 5 deterministic
- logs:
  - `logs/s05_6_2_mode1_staged_emit_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_mode1_staged_emit_74mhz_s05_fake_one_run.txt`
  - `logs/s05_6_2_mode1_staged_emit_74mhz_mode0_B64P0_repeat5_board.txt`
  - `logs/s05_6_2_mode1_staged_emit_74mhz_mode1_B64P0_dump_board.txt`
  - `logs/s05_6_2_mode1_staged_emit_74mhz_mode1_B64P0_repeat5_board.txt`

Interpretation: this candidate is rejected. It fixes the original lane4/lane12 mode1 diagnostic but leaves a deterministic mode1 corruption on block0/lane8.

## Board Candidate 5

Candidate tag:

- `s05_6_2_mode1_final_apply_snapshot_74MHz`

Intent:

- Single RTL delta from `fixed_128_mac`.
- Keep the mode0 board-PASS path unchanged.
- Avoid post-block dynamic reads of `block_acc[]` in mode1.
- During `ST_WEIGHT_APPLY`, when `mode_reg=1`, write the same next block accumulator value to both `block_acc[]` and existing `row_out[]`.
- Emit mode1 results from `row_out[]`.

Current source hash:

- stream core: `15cf5c06016ae268d08f2159ad1bf04c9b144f2ceff191ccdd2a394d27b31af6`
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

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_final_apply_snapshot_74MHz.bit`
- bitstream sha256: `d2dbebc6e4bd62af0f343b1dfdb3223d7c138f706c9a9c27c21f5429afadf81a`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_final_apply_snapshot_74MHz.xsa`
- XSA sha256: `7c6db37c362cb9b85c6dc02124893bb2c9ce83d11090c09f62a8bc7bb79075b6`
- timing: PASS, setup WNS `0.832 ns`, hold WHS `0.010 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_mode1_final_apply_snapshot_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `763415d432fe7f20cae0d010245f40e54f74d4c05a5c46f8d877100a25b6502c`
- FSBL: known-good S03, sha256 `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: known-good S03, sha256 `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash
- previous SD BOOT backup: `/dev/sdb1:/BOOT_BEFORE_TEST_S05_6_2_MODE1_FINAL_APPLY_SNAPSHOT_74MHZ_S03_FSBL_S03_UBOOT_20260702_065014.BIN`
- previous SD BOOT backup sha256: `30131d75e893dd144fef7081125fcbd2f7944ee07733e6f9ee32230ea888ac95`

Board result:

- gate 1 S05 fake one-run: FAIL
- mode0 fake output:
  - result0 got `-48`, expected `-48`
  - result1 got `19`, expected `19`
  - result2 got `-2147483648` (`0x80000000`), expected `-6`
- mode1 fake output: PASS
- gate stopped at step 1
- logs:
  - `logs/s05_6_2_mode1_final_apply_snapshot_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_mode1_final_apply_snapshot_74mhz_s05_fake_one_run.txt`

Interpretation: this candidate is rejected. Writing mode1 next accumulator values into `row_out[]` during `ST_WEIGHT_APPLY` changed synthesized mode0 fake behavior.

## Board Candidate 6

Candidate tag:

- `s05_6_2_mode1_scalar_snapshot_74MHz`

Intent:

- Single RTL delta from `fixed_128_mac`.
- Keep mode0 `row_out[]` and scale/accumulation path unchanged.
- Avoid post-block dynamic reads of `block_acc[]` in mode1.
- At `ST_BLOCK_DONE`, when `mode_reg=1`, copy `block_acc[0..15]` into scalar registers `mode1_out0..mode1_out15`.
- Emit mode1 results from those scalar registers through a fixed mux.

Current source hash:

- stream core: `cfe9cc81dad230bac9866545416334f332efef9ce226f4e893774a14f5f0c64e`
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

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_scalar_snapshot_74MHz.bit`
- bitstream sha256: `a52219b53fa2ba02718f71be3ff02422889644c16e8ab22895d280bf0fe78e74`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_scalar_snapshot_74MHz.xsa`
- XSA sha256: `c4706476d9dc026e4299acf3f68c5b806ae8dc1347e258d1abc1dba27fc7a82f`
- timing: PASS, setup WNS `0.364 ns`, hold WHS `0.018 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_mode1_scalar_snapshot_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `8438d93e89261a51e33fe22f03e8e9ca36bc360897b455eb935be575e4901541`
- FSBL: known-good S03, sha256 `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: known-good S03, sha256 `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash
- previous SD BOOT backup: `/dev/sdb1:/BOOT_BEFORE_TEST_S05_6_2_MODE1_SCALAR_SNAPSHOT_74MHZ_S03_FSBL_S03_UBOOT_20260702_070256.BIN`
- previous SD BOOT backup sha256: `763415d432fe7f20cae0d010245f40e54f74d4c05a5c46f8d877100a25b6502c`

Board result:

- gate 1 S05 fake one-run: FAIL
- mode0 fake output:
  - result0 got `-48`, expected `-48`
  - result1 got `19`, expected `19`
  - result2 got `-8388614` (`0xff7ffffa`), expected `-6`
- mode1 fake output: PASS
- gate stopped at step 1
- logs:
  - `logs/s05_6_2_mode1_scalar_snapshot_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_mode1_scalar_snapshot_74mhz_s05_fake_one_run.txt`

Interpretation: this candidate is rejected. Adding core-internal scalar snapshot registers still changes synthesized mode0 fake behavior.

## Board Candidate 7

Candidate tag:

- `s05_6_2_dma_top_output_register_74MHz`

Intent:

- Single RTL delta from `fixed_128_mac`.
- Keep `stream_core` exactly at the board-tested `fixed_128_mac` source.
- Add a one-entry AXIS register slice in `gemv_q8_0_dma_top.v` between core output and S2MM-facing `M_AXIS`.
- This targets a wrapper/S2MM timing or net handoff issue without adding fanout to core arithmetic/state.

Current source hash:

- stream core: `b9bac42726514a368b7557118da64ca177957aa8b6cccfb5ce8c39c9207fea16`
- dma top: `f50e9caca4959386cf1a330238630077d6a97c74d37d10cd442a5fd6851f859a`
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

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_dma_top_output_register_74MHz.bit`
- bitstream sha256: `217fab8067fa873402a053d866c1c3779dfc818f1a83163b490541c2ccca61fa`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_dma_top_output_register_74MHz.xsa`
- XSA sha256: `c7f87ccc2957dbb0ce472aae51290436a832cf7939b4f77eca3c216073b94c77`
- timing: PASS, setup WNS `0.758 ns`, hold WHS `0.013 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_dma_top_output_register_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `8b49e2a2ee23cfc14534831609f7c156e9efce89c92cecb57d70d9a1f266e8cb`
- FSBL: known-good S03
- U-Boot: known-good S03
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash

Board result:

- gate 1 S05 fake one-run: PASS
- gate 2 `B_64x16_P0` mode0 repeat5: PASS
- gate 3 `B_64x16_P0` mode1 dump: FAIL
- first mode1 mismatch: block0 lane4 got `8388682` (`0x0080004a`), expected `10`
- lane4:
  - block0 got `8388682`, expected `10`
  - block1 got `8388691`, expected `19`
- lane12:
  - block0 got `0`, expected `0`
  - block1 got `-8388619`, expected `-11`
- other repeated boundary-lane failures:
  - block0 lane8 got `-8388645`, expected `-37`
  - block1 lane8 got `-8388646`, expected `-38`
- logs:
  - `logs/s05_6_2_dma_top_output_register_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_dma_top_output_register_74mhz_s05_fake_one_run.txt`
  - `logs/s05_6_2_dma_top_output_register_74mhz_mode0_B64P0_repeat5_board.txt`
  - `logs/s05_6_2_dma_top_output_register_74mhz_mode1_B64P0_dump_board.txt`

Interpretation: this candidate is rejected as a full fix. It preserves the fixed `mode0` behavior but does not fix mode1. The corrupt values differ from expected by exactly `+/-0x00800000`; this points at `M_AXIS_TDATA[23]` contamination on the mode1 direct `block_acc[emit_lane]` emission path, not a DMA length, TLAST, or word-count problem.

## Board Candidate 8

Candidate tag:

- `s05_6_2_mode1_emit_load_stage_74MHz`

Intent:

- Single additional RTL delta after Candidate 7.
- Keep mode0 scaled accumulation path unchanged.
- Keep `dma_top` one-entry S2MM-facing output register from Candidate 7.
- Add one dedicated mode1 output load state/register in `stream_core`:
  - `ST_EMIT_BLOCK_LOAD` captures `block_acc[emit_lane]` into `mode1_emit_data_reg`.
  - `ST_EMIT_BLOCK` emits `mode1_emit_data_reg`.
- This targets the Candidate 7 board failure pattern where `M_AXIS_TDATA[23]` appeared to retain the previous lane's bit on some mode1 boundary lanes.

Current source hash:

- stream core: `b4ce90918c99d3604172187d0ab85815aebf7ba7ad9edac7973fb8cbb63f409e`
- dma top: `f50e9caca4959386cf1a330238630077d6a97c74d37d10cd442a5fd6851f859a`
- ctrl axi-lite: `5657ca43fe0283896160dc9ee3fb1fbbcabe5918869296267678a9711609e18e`
- board C diagnostic: `7843f64fabd1ca392e47f45840ec0b455da48b930ce48678fdd3293806febe6b`

Pre-board snapshot:

- folder: `artifacts/s05_6_2_prebuild_backup_20260702_0723_mode1_emit_load_stage/`

Pre-board simulation:

- wrapper-level `dma_top` sim: PASS
- wrapper sim run dir: `logs/s05_6_2_dma_top_xsim_20260702_072331/`
- core-level `stream_core` multi-block sim: PASS
- core sim run dir: `logs/s05_6_1_xsim_20260702_072345/`
- wrapper sim mode0 lane4: got `29`, expected `29`
- wrapper sim mode0 lane12: got `-11`, expected `-11`
- wrapper sim mode1 lane4/lane12:
  - block0 lane4 got `10`, expected `10`
  - block0 lane12 got `0`, expected `0`
  - block1 lane4 got `19`, expected `19`
  - block1 lane12 got `-11`, expected `-11`

Build result:

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_load_stage_74MHz.bit`
- bitstream sha256: `8e8a6ef628f030ed23627744fe73ab272c5d83d662ce49eca08808f9aa269755`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_load_stage_74MHz.xsa`
- XSA sha256: `cd3a42811d561b97e4d7aecda80e2d6685bf93a157dd271a1496957417986be3`
- timing: PASS, setup WNS `0.632 ns`, hold WHS `0.006 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_mode1_emit_load_stage_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `a990b8e014adb3611aa53d78382715df6667d81f616aa70790c90912e30fa0aa`
- FSBL: known-good S03
- U-Boot: known-good S03
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash

Board result:

- gate 1 S05 fake one-run: PASS
- gate 2 `B_64x16_P0` mode0 repeat5: PASS
- gate 3 `B_64x16_P0` mode1 dump: FAIL
- first mode1 mismatch: block0 lane4 got `33554442` (`0x0200000a`), expected `10`
- lane4:
  - block0 got `33554442`, expected `10`
  - block1 got `34603035`, expected `19`
- lane12:
  - block0 got `4`, expected `0`
  - block1 got `-555745291` (`0xdedffff5`), expected `-11`
- logs:
  - `logs/s05_6_2_mode1_emit_load_stage_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_mode1_emit_load_stage_74mhz_s05_fake_one_run.txt`
  - `logs/s05_6_2_mode1_emit_load_stage_74mhz_mode0_B64P0_repeat5_board.txt`
  - `logs/s05_6_2_mode1_emit_load_stage_74mhz_mode1_B64P0_dump_board.txt`

Interpretation: this candidate is rejected. A one-cycle mode1 direct-emission load register changes the corruption pattern but does not fix mode1. `mode0` remains clean, which continues to point at the mode1-only direct output structure rather than the common multiply/scale accumulation datapath or DMA length.

## Board Candidate 9

Candidate tag:

- `s05_6_2_mode1_reuse_scale_pipeline_74MHz`

Intent:

- Single RTL direction after Candidate 8 rejection.
- Keep Candidate 7 `dma_top` output register.
- Remove mode1-only direct `block_acc -> M_AXIS` emission.
- Reuse the board-tested mode0 scale/`row_out`/AXIS emission pipeline for mode1.
- For mode1 only, ignore packet scale values during scale application and use synthetic `scale_q_reg = 1 << scale_shift_reg`. Since the existing pipeline computes `(block_acc * scale_q) >> scale_shift`, this emits the raw `block_acc` value after the same rounding/saturation and `row_out` path used by mode0.

Current source hash:

- stream core: `4a4f07301c1dc9dad0151e20ec6a2aed6a4fc9dcdbcf11d89697a51e8d2ba165`
- dma top: `f50e9caca4959386cf1a330238630077d6a97c74d37d10cd442a5fd6851f859a`
- ctrl axi-lite: `5657ca43fe0283896160dc9ee3fb1fbbcabe5918869296267678a9711609e18e`
- board C diagnostic: `7843f64fabd1ca392e47f45840ec0b455da48b930ce48678fdd3293806febe6b`

Pre-board simulation:

- wrapper-level `dma_top` sim: PASS
- wrapper sim run dir: `logs/s05_6_2_dma_top_xsim_20260702_073925/`
- core-level `stream_core` multi-block sim: PASS
- core sim run dir: `logs/s05_6_1_xsim_20260702_073938/`
- wrapper sim mode0 lane4: got `29`, expected `29`
- wrapper sim mode0 lane12: got `-11`, expected `-11`
- wrapper sim mode1 lane4/lane12:
  - block0 lane4 got `10`, expected `10`
  - block0 lane12 got `0`, expected `0`
  - block1 lane4 got `19`, expected `19`
  - block1 lane12 got `-11`, expected `-11`

Build result:

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_reuse_scale_pipeline_74MHz.bit`
- bitstream sha256: `e0154d61ae4b685a31ce963bec8973aa8fc35ceb69c6d9d5849ac1b12d539bb4`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_reuse_scale_pipeline_74MHz.xsa`
- XSA sha256: `d0dd7720f4a06ea2916356ad8683d3653ee107b6bcf25860932613219bdf1525`
- timing: PASS, setup WNS `0.838 ns`, hold WHS `0.008 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_mode1_reuse_scale_pipeline_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `16862d78d2b7a4b7a4799994d7f2d89508bf4e6828ef0e4956a26affde465818`
- FSBL: known-good S03
- U-Boot: known-good S03
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash

Board result:

- first board gate attempt: rc=2 because Linux boot had not reached root prompt yet
- retry gate 1 S05 fake one-run: PASS
- retry gate 2 `B_64x16_P0` mode0 repeat5: FAIL
- first mode0 mismatch: lane4 got `-2147483648` (`0x80000000`), expected `29`
- repeated mode0 failures:
  - lane4 got `-2147483648`, expected `29`
  - lane8 got `-13`, expected `-75`
  - lane12 got `-536870911`, expected `-11`
- debug showed synthetic identity scale values in a mode0 multi-block run:
  - `scale0=1048576`, `scale1=1048576`, `scale2=1048576`
  - `scaled0=6`, `scaled1=-38`, `scaled2=58`
- logs:
  - `logs/s05_6_2_mode1_reuse_scale_pipeline_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_mode1_reuse_scale_pipeline_74mhz_s05_fake_one_run.txt`
  - `logs/s05_6_2_mode1_reuse_scale_pipeline_74mhz_mode0_B64P0_repeat5_board.txt`

Interpretation: this candidate is rejected. Reusing the common scale path with a mode mux in `ST_SCALE_LOAD` perturbs board mode0 multi-block behavior. The next candidate must keep the mode0 scale path structurally identical to Candidate 7 and isolate identity-scale work in mode1-only states.

## Board Candidate 10

Candidate tag:

- `s05_6_2_mode1_isolated_identity_scale_74MHz`

Intent:

- Keep Candidate 7 `dma_top` output register.
- Keep the mode0 `ST_SCALE_LOAD` / `ST_SCALE_MUL` / `ST_SCALE_SHIFT` / `ST_SCALE_ACCUM` / `ST_SCALE_SAT` path structurally identical to Candidate 7.
- Add mode1-only identity-scale states:
  - `ST_MODE1_SCALE_LOAD`
  - `ST_MODE1_SCALE_MUL`
  - `ST_MODE1_SCALE_SHIFT`
  - `ST_MODE1_SCALE_SAT`
- In mode1, use `scale_q_reg = 1 << scale_shift_reg`, write the raw block accumulator result into `row_out[emit_lane]`, then emit from `row_out[]`.
- This avoids the Candidate 9 mode mux in the mode0 scale path while still avoiding direct `block_acc[emit_lane] -> M_AXIS` emission.

Current source hash:

- stream core: `c2bb16ff5b035fcef2831108147009073ee26beaa353e64f25f4ad49a05d3e58`
- dma top: `f50e9caca4959386cf1a330238630077d6a97c74d37d10cd442a5fd6851f859a`
- ctrl axi-lite: `5657ca43fe0283896160dc9ee3fb1fbbcabe5918869296267678a9711609e18e`
- board C diagnostic: `7843f64fabd1ca392e47f45840ec0b455da48b930ce48678fdd3293806febe6b`

Pre-board simulation:

- wrapper-level `dma_top` sim: PASS
- wrapper sim run dir: `logs/s05_6_2_dma_top_xsim_20260702_075706/`
- core-level `stream_core` multi-block sim: PASS
- core sim run dir: `logs/s05_6_1_xsim_20260702_075722/`
- wrapper sim mode0 lane4: got `29`, expected `29`
- wrapper sim mode0 lane12: got `-11`, expected `-11`
- wrapper sim mode1 lane4/lane12:
  - block0 lane4 got `10`, expected `10`
  - block0 lane12 got `0`, expected `0`
  - block1 lane4 got `19`, expected `19`
  - block1 lane12 got `-11`, expected `-11`

Build result:

- bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.bit`
- bitstream sha256: `e512452259e69914e97a134ff6d781c03dd0fbf4a7fe0ca8497f0f3befd03d23`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.xsa`
- XSA sha256: `4dea0c4cbf2d58acaf920afbfadf07d234fb6cdeb487818263780ba539e551ed`
- timing: PASS, setup WNS `0.591 ns`, hold WHS `0.008 ns`
- latest aliases skipped with `GPTALK_UPDATE_LATEST=0`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_6_2_mode1_isolated_identity_scale_74mhz_s03_fsbl_s03_uboot/`
- BOOT.BIN sha256: `472ae39924e2f25f5ae62dd8141d0f3d1669352e97e279b76372c17b28debb5e`
- FSBL: known-good S03
- U-Boot: known-good S03
- SD `/dev/sdb1:/BOOT.BIN` staged and verified with the same hash

Board result:

- gate 1 boot/root prompt and S05 fake one-run: PASS
- gate 2 `B_64x16_P0` mode0 repeat5: PASS
  - original failing lane4 is fixed: board output matches expected, repeat 5 deterministic PASS
  - status `0x00000002`, error clear, final debug row/block/lane `15/1/15`
- gate 3 `B_64x16_P0` mode1 dump: PASS
  - lane4/lane12 diagnostic is fixed
  - `MODE1CSV,B_64x16_P0,64x16,P0,2,1152,128,PASS,PASS,0,0,0,0,73`
- gate 4 full S05.6.1 multi-block: PASS
  - A/B/C/D/D2/E cases PASS through `E_576x16_P9`
  - `F_1536x16` is deferred by the known DMA simple length limit: packet `27648`, limit `16383`
- gate 5 S05.5 fake 100-run: PASS
  - mode0 fail_count `0`
  - mode1 fail_count `0`
- gate 6 S05.6 batch/proxy after multi-block PASS: PASS on rerun
  - first run produced `OVERALL PASS` but harness returned rc `8` because its bad-marker scan matched the host echo of the `grep ... external abort ...` command string
  - harness was narrowed to ignore `### HOST_SEND:` lines during bad-marker scanning
  - rerun rc `0`, `fake_gemv mode=0 batch PASS`, `fake_gemv mode=1 batch PASS`, `proxy benchmark PASS`, `OVERALL PASS`
- logs:
  - `logs/s05_6_2_mode1_isolated_identity_scale_74mhz_board_gate_summary.txt`
  - `logs/s05_6_2_mode1_isolated_identity_scale_74mhz_s05_fake_one_run.txt`
  - `logs/s05_6_2_mode1_isolated_identity_scale_74mhz_mode0_B64P0_repeat5_board.txt`
  - `logs/s05_6_2_mode1_isolated_identity_scale_74mhz_mode1_B64P0_dump_board.txt`
  - `logs/s05_6_2_mode1_isolated_identity_scale_74mhz_full_multiblock_board.txt`
  - `logs/s05_6_2_mode1_isolated_identity_scale_74mhz_s05_5_fake_quiet_100.txt`
  - `logs/s05_6_2_mode1_isolated_identity_scale_74mhz_s05_6_batch_benchmark_rerun.txt`

Root cause:

- The original mode0 board failure was fixed by the `fixed_128_mac` stream-core arithmetic/state change from Candidate 1: the 128-bit path now updates all 16 lane accumulators explicitly in the MAC apply state instead of sharing the narrower lane-window update structure.
- The remaining board-only mode1 failures came from direct synthesized emission of live block accumulator values. Several candidates showed mode0 PASS while mode1 alone failed.
- Candidate 10 resolves that by routing mode1 through isolated identity-scale states and the staged `row_out[]` output path, while preserving the known board-PASS mode0 scale path and the wrapper output register.

Final status:

- S05.6.2 PASS.
- S06 not started.
- Next step is S05.6.3 DMA length/chunking strategy for `F_1536x16` and real `down_proj` / `lm_head`-class tensors.

## Future Blocker: DMA Length

The current AXI DMA simple-mode length width is `C_SG_LENGTH_WIDTH=14`, so one simple transfer is limited to 16383 bytes.

This is not the cause of the current `B_64x16_P0` failure because that packet is 1152 bytes.

The length limit remains a real future blocker for `F_1536x16` and real `down_proj` / `lm_head`-class tensors. S05.6.2 must not change `C_SG_LENGTH_WIDTH`, switch to scatter-gather DMA, or use DMA-length changes to mask the `B_64x16_P0` correctness bug. After S05.6.2 passes, move this to S05.6.3 as a DMA length/chunking strategy task.
