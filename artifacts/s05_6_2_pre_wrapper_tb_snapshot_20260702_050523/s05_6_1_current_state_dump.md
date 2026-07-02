# S05.6.1 Current State Dump

Created: 2026-07-01 KST

All paths in this document are relative to the repository root.

## Short Verdict

S03 boot recovery and S05.5 128-bit fake GEMV are valid. S05.6 batching reduced host overhead for the one-block fake GEMV case, but the real-workload proxy failed. The failure is now narrowed to `blocks_per_row > 1` mode=0 scaled accumulation under the 128-bit AXIS path.

S06 is still blocked.

The latest board-validated candidate, `state_encoding_preserve_74MHz`, boots and passes one-block fake GEMV, but fails deterministic multi-block board tests. The current RTL source is one small step beyond that candidate: it preserves the state-encoding/scale-pipeline changes and changes only the 128-bit MAC update indexing. This current source has passed expanded core-level RTL simulation, but it has not yet been synthesized, packaged into BOOT.BIN, or board-validated.

## Hard Gates

- Do not enter S06 until explicitly requested.
- Do not treat S05.6 as complete while real-workload proxy is failing.
- Do not overwrite the S05.5 128-bit baseline BOOT/bitstream/XSA.
- Do not overwrite the 32-bit known-good aliases.
- Do not reuse rejected S05.2 counter bitstreams/BOOTs.
- Do not use AXI-Lite bulk data path.
- Do not disable TLAST/TKEEP checks.
- Do not revert the `valid_lanes_reg` output emit correctness fix.
- Do not shrink lanes to hide the bug.
- Do not do a PetaLinux full rebuild for this issue.
- Do not use custom/recovery FSBL.
- If a new BOOT is needed, stage it through the PC SD-card path unless explicitly told otherwise.

## Active Project And Repo State

- Active Vivado project found by `find hw -name '*.xpr' -print`:
  - `hw/vivado_project/GPTalk.xpr`
- There are many untracked S05.6/S05.6.1 artifacts and logs.
- `docs/00_ACTIVE_KR.md` is stale/spaghetti and should be cleaned after the next definitive board result.
- Current source tree is dirty; this document is a snapshot, not a clean release point.

## Frozen Baselines

### 32-bit known-good alias

These are the current `GPTalk_dma.*` aliases. They are not the S05.5 128-bit baseline.

- `hw/vivado_project/export/GPTalk_dma.bit`
  - sha256: `158ba9de633fc8ea4a8b4822d0589fad427ec25502ff67cc8d44ef604696acb0`
- `hw/vivado_project/export/GPTalk_dma.xsa`
  - sha256: `f6ef5281f7558d21435127cc6858e72d312089e1f7a26bed34ce4206ae9a1d7d`

Relevant backup:

- `artifacts/s05_3_validated_known_good_reexport_20260701_010926/`

### S05.5 128-bit board-validated baseline

This is the important 128-bit known-good baseline before S05.6.1 debugging.

- BOOT:
  - `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot/BOOT.BIN`
  - sha256: `4d7f875198fed7806b6265db126909067587ee42ec3e5db32308fd62dbd59a8c`
- bitstream:
  - `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.bit`
  - sha256: `1f873bc39b48d56275f9f07e7fd5db1b979adc118beff768c0b24a9204a7d4ac`
- XSA:
  - `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.xsa`
  - sha256: `005b3f9b04cc521fca0c4979d77a8c19cdab9c0335a477cb973e48a7ec627f08`
- FSBL:
  - source: `artifacts/s03_bootgen/zynq_fsbl.elf`
  - sha256: `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot:
  - source: `artifacts/s03_bootgen/u-boot.elf`
  - sha256: `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- clock:
  - `74 MHz`
- timing:
  - PASS
  - WNS `0.389 ns`
  - WHS `0.018 ns`
- GEMV build config:
  - `BUILD_CONFIG=0x00800010`
  - `axis_width=128`
  - `lanes=16`
- S05.5 board result:
  - DONE LED: user reported on
  - Linux root prompt: detected
  - mode=0 scaled: PASS, `[-48, 19, -6]`
  - mode=1 block_acc: PASS, `[-193, 38, -50]`
  - quiet 100-run: PASS, fail_count `0`
  - AXI DMA MM2S/S2MM used: yes
  - input BRAM used: yes
  - AXI-Lite bulk path used: no
  - no kernel panic/oops/bus error observed
- Reference logs:
  - `docs/s05_5_128bit_axis_bringup.md`
  - `logs/s05_5_axis128_bram_scalar_board_full_debug.txt`
  - `logs/s05_5_axis128_bram_scalar_board_quiet_100.txt`
  - `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot/MANIFEST.txt`

## Board/Linux Access Facts

Latest board checks used the same Linux/user-space access pattern:

- kernel command line includes:
  - `console=ttyPS0,115200`
  - `uio_pdrv_genirq.of_id=generic-uio`
  - `root=/dev/mmcblk0p2 rw rootwait`
  - `mem=960M`
- UIO map:
  - `uio0` name `axi_dma`, addr `0x40400000`, size `0x00010000`
  - `uio1` name `input_bram`, addr `0x42000000`, size `0x00010000`
  - `uio2` name `hdmi_vdma`, addr `0x43010000`, size `0x00010000`
  - `uio3` name `hdmi_vtc`, addr `0x43c10000`, size `0x00010000`
  - `uio4` name `hdmi_dynclk`, addr `0x43c20000`, size `0x00010000`
  - `uio5` name `gemv_ctrl`, addr `0x43ca0000`, size `0x00001000`
- host-visible tool on board:
  - `/usr/bin/gcc`
- physical DMA buffer strategy:
  - `/dev/mem O_SYNC` carveout
  - base `0x3c000000`
  - size `0x04000000`
- AXI DMA simple length width:
  - `C_SG_LENGTH_WIDTH=14`
  - max simple transfer: `16383` bytes

## S05.6 Batching State

S05.6 added `runtime_c/gemv_batch_bench.c` with a reusable driver layer:

- `gemv_hw_open()`
- `gemv_hw_close()`
- `gemv_hw_reset_dma()`
- `gemv_hw_config_static()`
- `gemv_hw_config_job()`
- `gemv_hw_load_input()`
- `gemv_hw_prepare_packet()`
- `gemv_hw_run_one()`
- `gemv_hw_run_batch()`

UIO devices are looked up by `name`; UIO numbers are not hardcoded.

Selected fake-GEMV hot path:

- reset once per batch
- static config cache
- packet preloaded
- input reuse
- bounded busy poll

Representative batch result:

- `A_reset_every_job`, batch 256:
  - mode=0 avg/job about `48 us`, fail_count `0`
  - mode=1 avg/job about `48 us`, fail_count `0`
- `F_combined_hot_path`, batch 256:
  - mode=0 avg/job about `27 us`, fail_count `0`
  - mode=1 avg/job about `27 us`, fail_count `0`

Reference logs:

- `docs/s05_6_batching_persistent_job.md`
- `logs/s05_6_baseline_freeze.txt`
- `logs/s05_6_batch_benchmark.txt`
- `logs/s05_6_batch_benchmark.csv`

## S05.6 Proxy Failure

Fake one-block batching is not enough. The S05.6 real-workload proxy failed.

Proxy results from `logs/s05_6_batch_benchmark.txt`:

| proxy | chunk jobs | full packet bytes | result |
|---|---:|---:|---|
| `lane_probe_32x16` | 1 | 576 | PASS |
| `mlp_576x1536_chunked` | 96 | 995328 | FAIL |
| `down_1536x576_chunked` | 108 | 995328 | FAIL |
| `lm_head_576x256_chunked` | 16 | 165888 | FAIL |

Failure examples:

- `mlp_576x1536_chunked_chunk_576x16 mode=0 result[12] got=-2147483648 expected=220`
- `down_1536x576_chunked_chunk_512x16 mode=0 result[13] got=39 expected=37`
- `lm_head_576x256_chunked_chunk_576x16 mode=0 result[8] got=2147483647 expected=152`

Interpretation:

- The one-block 32x16 lane probe passes.
- Failures start when `blocks_per_row > 1`.
- Full S05.4 tensor packets cannot be sent as one simple-mode DMA transfer because the DMA max length is `16383` bytes.
- Even TLAST-safe chunked proxy still fails correctness, so increasing DMA length width alone would hide the transfer-size issue but not fix the multi-block correctness bug.

## S05.6.1 Test Set

Generated by:

- `scripts/s05_6_1_multiblock_reference.py`

Outputs:

- `golden/s05_6_1_multiblock/`
- `logs/s05_6_1_multiblock_reference.txt`
- `reports/s05_6_1_multiblock_expected.csv`

Deterministic shapes:

| name | in_features | out_features | blocks_per_row | packet bytes | DMA simple ok |
|---|---:|---:|---:|---:|---|
| `A_32x16` | 32 | 16 | 1 | 576 | yes |
| `B_64x16` | 64 | 16 | 2 | 1152 | yes |
| `C_96x16` | 96 | 16 | 3 | 1728 | yes |
| `D_512x16` | 512 | 16 | 16 | 9216 | yes |
| `D2_544x16` | 544 | 16 | 17 | 9792 | yes |
| `E_576x16` | 576 | 16 | 18 | 10368 | yes |
| `F_1536x16` | 1536 | 16 | 48 | 27648 | no, deferred |

Patterns used:

- `P0`: all blocks active, scale identity
- `P1`: block0 only nonzero
- `P2`: last block only nonzero
- `P3`: scale differs by block
- `P4`: input pattern exposes block index
- `P5`: lane-specific weight pattern
- `P6` and later: proxy-oriented patterns for larger shapes

## Latest Board-Validated Candidate: state_encoding_preserve_74MHz

Candidate:

- test name:
  - `test_s05_6_1_state_encoding_preserve_74mhz_s03_fsbl_s03_uboot`
- BOOT:
  - `artifacts/boot_tests/test_s05_6_1_state_encoding_preserve_74mhz_s03_fsbl_s03_uboot/BOOT.BIN`
  - sha256: `b91122815852ab4f2c231c2ff78b3e730bf677e36e25845fa83193f699c3e2b1`
- bitstream:
  - `hw/vivado_project/export/GPTalk_dma_s05_6_1_state_encoding_preserve_74MHz.bit`
  - sha256: `861c500d33dfc56c90d02057c7b6f9289cbd7d9de62afca9bb2d094e95f3d458`
- XSA:
  - `hw/vivado_project/export/GPTalk_dma_s05_6_1_state_encoding_preserve_74MHz.xsa`
  - sha256: `021f2106548f8002150556ea4308fbde016f92587df302aceb5038d1c26e649a`
- FSBL:
  - known-good S03 FSBL
  - sha256: `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot:
  - S03 U-Boot
  - sha256: `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- timing:
  - PASS
  - WNS `0.564 ns`
  - WHS `0.016 ns`
- SD staging:
  - staged to `/BOOT.BIN`
  - previous backup on SD had hash `b10f884b2794d24a1c18eb00da9519f6c483f6ea0ae92025961bf402f95dce49`
  - that backup is the rejected scale operand pipeline BOOT and should be removed when the SD is next mounted on the PC

### state_encoding_preserve fake S05 result

S05 fake one-block still passes:

- log:
  - `logs/s05_6_1_state_encoding_preserve_74mhz_s05_fake_short.txt`
- mode=0 scaled:
  - PASS
  - `[-48, 19, -6]`
- mode=1 block_acc:
  - PASS
  - `[-193, 38, -50]`
- AXI DMA used:
  - yes
- input BRAM used:
  - yes
- AXI-Lite bulk path:
  - no
- kernel panic/oops/bus error:
  - none observed

### state_encoding_preserve multi-block board result

Full board multi-block result:

- log:
  - `logs/s05_6_1_state_encoding_preserve_74mhz_multiblock_board.txt`
- CSV:
  - `reports/s05_6_1_state_encoding_preserve_74mhz_multiblock_board.csv`
- result:
  - FAIL
  - fail_count `12`

Important facts:

- `A_32x16_P0` PASS
- `B_64x16_P1` PASS
- `B_64x16_P4` PASS
- Failures are deterministic for multi-block cases.
- DMA status is normal on failures:
  - MM2S `0x00001002`
  - S2MM `0x00001002`
  - GEMV STATUS `0x00000002`
  - no GEMV ERROR_CODE/TLAST failure in these tests
- Debug counters look structurally consistent:
  - `debug_in_count = beats`
  - `debug_tlast_count = beats - 1`
  - last debug block/lane reaches expected final block/lane

Key failure examples:

| case | classification | first mismatch | got | expected |
|---|---|---:|---:|---:|
| `B_64x16_P0` | `ROW_ACC_ACCUM_FAIL` | lane 4 | 37 | 29 |
| `B_64x16_P2` | `INPUT_ADDR_PROGRESSION_FAIL` | lane 4 | 27 | 19 |
| `B_64x16_P3` | `SCALE_BLOCK_PROGRESSION_FAIL` | lane 4 | 64 | 48 |
| `B_64x16_P5` | `WEIGHT_BLOCK_PROGRESSION_FAIL` | lane 4 | -10 | -18 |
| `C_96x16_P0` | `ROW_ACC_ACCUM_FAIL` | lane 4 | 60 | 52 |
| `C_96x16_P3` | `SCALE_BLOCK_PROGRESSION_FAIL` | lane 4 | 156 | 140 |
| `D_512x16_P0` | `ROW_ACC_ACCUM_FAIL` | lane 4 | 5 | -19 |
| `E_576x16_P0` | `ROW_ACC_ACCUM_FAIL` | lane 4 | -23 | -55 |
| `E_576x16_P3` | `SCALE_BLOCK_PROGRESSION_FAIL` | lane 4 | -60 | -140 |

The most useful minimized reproducer is:

- `B_64x16_P0`
- `blocks_per_row=2`
- first mismatch lane `4`
- got `37`
- expected `29`
- repeated 5 times with identical mismatch
- log:
  - `logs/s05_6_1_state_encoding_preserve_74mhz_mode0_B64P0_only_board.txt`
- CSV:
  - `reports/s05_6_1_state_encoding_preserve_74mhz_mode0_B64P0_only_board.csv`

This repeat test reduces the chance that the failure is prior-case residue. It is deterministic and local to a small two-block case.

### mode=1 block diagnostic

Mode1 long-output diagnostic was added to `runtime_c/gemv_multiblock_test.c` and run on `B_64x16_P0`:

- log:
  - `logs/s05_6_1_state_encoding_preserve_74mhz_mode1_B64P0_dump_board.txt`
- CSV:
  - `reports/s05_6_1_state_encoding_preserve_74mhz_mode1_B64P0_dump_board.csv`

Result:

- FAIL
- mode1 result bytes: `128`
- first mismatch:
  - block `0`
  - lane `4`
  - got `6291466`
  - expected `10`

Important lane diagnostics:

- lane 4:
  - block0 got `6291466`, expected `10`
  - block1 got `6291483`, expected `19`
- lane 12:
  - block0 got `12`, expected `0`
  - block1 got `-2097163`, expected `-11`

This is more severe than mode0. It may be a separate mode1 output contract/path issue, but it also confirms that multi-block/long-output lane 4 and lane 12 behavior diverges on board while one-block fake mode1 still passes.

## Rejected Or Non-Active Candidates

Multiple S05.6.1 candidate BOOT folders exist. They should not be treated as active merely because they exist.

Candidate folders:

- `artifacts/boot_tests/test_s05_6_1_inline_sat_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_min_sat_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_multiblock_signed_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_muxsat_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_rowout_signed_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_sat_pipeline_50mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_sat_pipeline_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_scale_operand_pipeline_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_state_encoding_preserve_74mhz_s03_fsbl_s03_uboot/`

Explicitly rejected:

- `scale_operand_pipeline_74MHz`
- backup:
  - `artifacts/s05_6_1_rejected_scale_operand_pipeline_20260701_2223/`
- BOOT sha256:
  - `b10f884b2794d24a1c18eb00da9519f6c483f6ea0ae92025961bf402f95dce49`
- bitstream sha256:
  - `8ab39661a8a66eed9e37f39f14ba09076b1400b2343dc397f7e11398c1510152`
- XSA sha256:
  - `d2b69dcc9708a6af02ab2ba7c40794785bc2882749f5bb436a3c5c0cb424e2c7`
- reason:
  - board booted, but S05 fake mode=0 and mode=1 both failed
  - not a valid baseline

Non-active / failed for S05.6.1:

- `state_encoding_preserve_74MHz`
- reason:
  - fake one-block S05 passes
  - multi-block S05.6.1 fails
  - cannot be used for S06

## Current Source State

Current source hashes:

- `vivado_ip/rtl/gemv_q8_0_stream_core.v`
  - sha256: `b9bac42726514a368b7557118da64ca177957aa8b6cccfb5ce8c39c9207fea16`
- `vivado_ip/rtl/gemv_q8_0_dma_top.v`
  - sha256: `eea8f8192ee62312b7ee8f25a87045f43e5c358554c2124a8c64c56bfdfb1dbf`
- `vivado_ip/tb/tb_gemv_q8_0_stream_core_multiblock.sv`
  - sha256: `5072eaa3ebfc39b2a2593163852966ec728ac229534c27f15921b139d33a2b58`
- `runtime_c/gemv_multiblock_test.c`
  - sha256: `cc2bd8755823064dfc9e5c6812eef54b294ab97a4396333911b62255b3006b82`
- `scripts/run_s05_6_1_multiblock_rtl_sim.sh`
  - sha256: `c2fe5118568c62f9bfde4b421efb41fff05c369124ded742e662c6300281394e`
- `scripts/s05_6_1_multiblock_board.py`
  - sha256: `e7e18df457038922b38b11dfa5177b5dea299621d492b7f6f4e2d782d2cdac99`
- `scripts/s05_6_1_multiblock_reference.py`
  - sha256: `78bff5793081656761aa794a6d7463951a59bb155d1f871aee75a723d1dc90aa`

Current source backup:

- `artifacts/s05_6_1_prebuild_backup_20260701_2250_fixed_128_mac/`
- `gemv_q8_0_stream_core.v` sha256:
  - `b9bac42726514a368b7557118da64ca177957aa8b6cccfb5ce8c39c9207fea16`
- `gemv_q8_0_dma_top.v` sha256:
  - `eea8f8192ee62312b7ee8f25a87045f43e5c358554c2124a8c64c56bfdfb1dbf`

Important current RTL note:

- Current source is not the same as the board-tested `state_encoding_preserve_74MHz` bitstream.
- Diff from `artifacts/s05_6_1_prebuild_backup_20260701_2228_state_encoding_preserve/gemv_q8_0_stream_core.v` is intentionally small:
  - for `TDATA_WIDTH == 128`, `ST_WEIGHT_APPLY` updates `block_acc[b]` directly for `b=0..LANES-1`
  - the old 32-bit path still uses `apply_lane_base + b`
- This is the `fixed_128_mac` source candidate.
- It has not yet been synthesized or tested on board.
- Explicit read mux helpers such as `read_scale_q`, `read_block_acc`, `read_row_acc`, `read_scaled_block`, `muxsat`, or `explicit_read_mux` are not present in current RTL.

## Latest RTL Simulation State

Core-level multi-block RTL simulation was expanded.

Modified files:

- `vivado_ip/tb/tb_gemv_q8_0_stream_core_multiblock.sv`
- `scripts/run_s05_6_1_multiblock_rtl_sim.sh`

New coverage:

- mode0 multi-block output checks
- mode1 multi-block long-output checks
- block/lane output metadata checks
- deterministic packet set from `reports/s05_6_1_multiblock_expected.csv`

Latest run:

- log:
  - `logs/s05_6_1_multiblock_rtl_sim.txt`
- run dir:
  - `logs/s05_6_1_xsim_20260701_224928/`
- result:
  - `S05_6_1_RTL_SIM_PASS`

Passing cases include:

- `A_32x16_P0`
- all `B_64x16_P0/P1/P2/P3/P4/P5`
- all selected `C_96x16`
- `D_512x16_P0`
- `D_512x16_P6`
- `D2_544x16_P6`
- all selected `E_576x16`
- mode1 checks for the same cases

Deferred:

- `F_1536x16_DEFER_DMA_LENGTH`
- reason:
  - packet bytes `27648`
  - DMA limit `16383`

Interpretation:

- The expanded core TB still does not reproduce the board failure.
- The problem is therefore likely outside the narrow behavioral core test boundary, or it depends on synthesis/top-wrapper/timing/AXIS/BRAM/control behavior not represented by the core TB.

## Current Root-Cause Analysis

The repeated S05.6.1 failure does not look like:

- boot/FSBL/BOOT packaging failure
- Linux/device-tree issue
- PetaLinux/rootfs issue
- board not alive
- UIO enumeration issue
- DMA register access issue
- input BRAM mmap/readback issue
- AXI DMA simple transfer completion failure
- TLAST mismatch in the current multi-block board tests
- kernel panic/oops/bus error

The failure does look like a multi-block 128-bit data-path correctness issue:

- One-block `32x16` passes.
- S05.5 fake `32x3` passes.
- S05.6 batch fake one-block passes.
- Multi-block `64x16` already fails.
- The smallest stable reproducer is `B_64x16_P0`.
- Lane 4 is the first recurring mismatch.
- Lane 12 is also suspicious in mode1 diagnostics.
- Debug counters indicate the stream length, final block, and final lane are reached.
- DMA status indicates normal MM2S/S2MM completion.

The likely verification blind spot:

- The existing core TB drives `gemv_q8_0_stream_core` directly.
- It does not fully model:
  - `gemv_q8_0_dma_top`
  - AXI-Lite register write ordering
  - wrapper-level control/reset behavior
  - input BRAM port timing as connected in the BD
  - 128-bit MM2S stream with realistic backpressure
  - 32-bit S2MM sink behavior
  - Vivado synthesis/implementation effects
- Therefore the TB can PASS while the board still fails.

This explains why repeatedly changing arithmetic details without expanding the verification boundary is risky. The most useful next test is not a broad new optimization; it is a wrapper-level TB around `gemv_q8_0_dma_top` focused on `B_64x16_P0`.

## Recommended Next Step

Do a wrapper-level simulation before another board spin.

Target:

- `vivado_ip/rtl/gemv_q8_0_dma_top.v`

Minimum wrapper-level TB requirements:

- instantiate `gemv_q8_0_dma_top`
- drive AXI-Lite config in the same order as the C driver
- use the deterministic `B_64x16_P0` packet
- model input BRAM data timing or use the same BRAM-facing signals the top wrapper sees
- drive 128-bit MM2S stream
- collect 32-bit S2MM output stream
- check:
  - output lane values
  - `m_axis_tlast`
  - output word count
  - mode0 result for all 16 lanes
  - mode1 block output if practical
- add controlled `TREADY` backpressure

Pass/fail interpretation:

- If wrapper-level TB reproduces lane 4/12 failure:
  - fix RTL before board build
- If wrapper-level TB passes but board still fails:
  - suspect synthesis/implementation, BD net connectivity, BRAM timing, or timing constraints
  - consider a narrow debug-register bitstream that latches lane 4/12 input, weight, mul, block_acc, scaled, and row_acc values

Only after that should a new bitstream be built.

If current `fixed_128_mac` source is built, it must be treated as a candidate:

- use a new export tag
- do not update generic aliases
- do not overwrite S05.5 known-good artifacts
- package with known-good S03 FSBL and S03 U-Boot
- stage through PC SD card
- board gate order:
  1. boot/DONE/root prompt
  2. S05 fake one-run
  3. `B_64x16_P0 --repeat 5`
  4. full S05.6.1 multi-block
  5. S05.5 fake 100-run
  6. S05.6 batch/proxy rerun only if multi-block passes

## S06 Gate Status

S06 entry: NO.

Missing requirements:

- S05.6 real-workload proxy PASS
- S05.6.1 64x16 multi-block PASS
- S05.6.1 576x16 multi-block PASS
- confirmed strategy for the `C_SG_LENGTH_WIDTH=14` DMA transfer limit
- active 128-bit candidate board-validated after fixes

## Files Most Relevant From Here

Core RTL:

- `vivado_ip/rtl/gemv_q8_0_stream_core.v`
- `vivado_ip/rtl/gemv_q8_0_dma_top.v`

Simulation:

- `vivado_ip/tb/tb_gemv_q8_0_stream_core_multiblock.sv`
- `scripts/run_s05_6_1_multiblock_rtl_sim.sh`
- next needed: wrapper-level TB for `gemv_q8_0_dma_top`

Board tests:

- `runtime_c/gemv_multiblock_test.c`
- `scripts/s05_6_1_multiblock_board.py`
- `runtime_c/gemv_batch_bench.c`
- `scripts/s05_6_batch_bench.py`
- `runtime_c/gemv_hw_test.c`
- `scripts/s05_gemv_hw_test.py`

Reference generation:

- `scripts/s05_6_1_multiblock_reference.py`
- `golden/s05_6_1_multiblock/`
- `reports/s05_6_1_multiblock_expected.csv`

Key logs:

- `logs/s05_6_1_multiblock_rtl_sim.txt`
- `logs/s05_6_1_state_encoding_preserve_74mhz_multiblock_board.txt`
- `logs/s05_6_1_state_encoding_preserve_74mhz_mode0_B64P0_only_board.txt`
- `logs/s05_6_1_state_encoding_preserve_74mhz_mode1_B64P0_dump_board.txt`
- `logs/s05_6_1_state_encoding_preserve_74mhz_s05_fake_short.txt`
- `logs/s05_6_batch_benchmark.txt`

Key reports:

- `reports/s05_6_1_state_encoding_preserve_74mhz_multiblock_board.csv`
- `reports/s05_6_1_state_encoding_preserve_74mhz_mode0_B64P0_only_board.csv`
- `reports/s05_6_1_state_encoding_preserve_74mhz_mode1_B64P0_dump_board.csv`
- `reports/s05_6_proxy_benchmark.csv`

Key artifacts:

- `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/boot_tests/test_s05_6_1_state_encoding_preserve_74mhz_s03_fsbl_s03_uboot/`
- `artifacts/s05_6_1_rejected_scale_operand_pipeline_20260701_2223/`
- `artifacts/s05_6_1_prebuild_backup_20260701_2250_fixed_128_mac/`
