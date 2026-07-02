# S05.6 Batching / Persistent-Job Overhead Reduction

## Scope

- Status: partial PASS / blocked for S06.
- S05.6 only. Do not proceed to S06 without explicit user request.
- Active 128-bit BOOT/bitstream/XSA are frozen from S05.5 and are not regenerated in this stage.
- The test path remains user-space AXI DMA + input BRAM + `/dev/mem` carveout.
- AXI-Lite bulk data path is not used.

## Frozen Baseline

- Freeze log: `logs/s05_6_baseline_freeze.txt`
- S05.5 reference: `docs/s05_5_128bit_axis_bringup.md`
- BOOT: `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot/BOOT.BIN`
- BOOT sha256: `4d7f875198fed7806b6265db126909067587ee42ec3e5db32308fd62dbd59a8c`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.bit`
- Bitstream sha256: `1f873bc39b48d56275f9f07e7fd5db1b979adc118beff768c0b24a9204a7d4ac`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.xsa`
- XSA sha256: `005b3f9b04cc521fca0c4979d77a8c19cdab9c0335a477cb973e48a7ec627f08`
- Clock: `74 MHz`
- Timing: PASS, WNS `0.389 ns`, WHS `0.018 ns`
- `BUILD_CONFIG=0x00800010`, `axis_width=128`, `lanes=16`
- Preserved 32-bit known-good backup: `artifacts/s05_3_validated_known_good_reexport_20260701_010926`

## Driver Split

S05.6 adds `runtime_c/gemv_batch_bench.c` with a reusable driver layer:

- `gemv_hw_open()`: UIO name lookup, AXI DMA/input BRAM/GEMV control mmap, carveout mmap.
- `gemv_hw_close()`: unmap and close all handles.
- `gemv_hw_reset_dma()`: AXI DMA reset.
- `gemv_hw_config_static()`: mode-independent/static GEMV register config.
- `gemv_hw_config_job()`: mode, shape, transfer length, and result length config.
- `gemv_hw_load_input()`: input BRAM load.
- `gemv_hw_prepare_packet()`: MM2S packet preload into carveout.
- `gemv_hw_run_one()`: DMA start, GEMV start, bounded busy poll, output check.
- `gemv_hw_run_batch()`: repeated jobs with reset/config/input/packet reuse policy.

UIO devices are found by `name`; UIO numbers are not hardcoded.

## Important Contract Note

The current simple-mode AXI DMA MM2S transfer can assert TLAST only at the end of one transfer. The GEMV stream core requires TLAST at each tensor packet end. Therefore S05.6 does not concatenate multiple GEMV jobs into one long MM2S transfer. This stage reduces per-job host overhead by avoiding unnecessary DMA reset, static config writes, packet memcpy, and input BRAM rewrites where the job contract permits it.

True persistent multi-job streaming would require a future RTL/DMA contract change, such as scatter-gather descriptors with per-packet TLAST, a packet-count aware RTL wrapper, or an explicit multi-job stream format.

## Variants

- A `reset-every-job`: DMA reset, full config, input write, and packet memcpy for every job.
- B `reset-once`: DMA reset once per batch, minimal per-job config.
- C `static-config-cache`: skip unchanged static register writes.
- D `packet-preloaded`: copy packet to carveout once per batch.
- E `input-reuse`: write input BRAM once per batch.
- F `combined-hot-path`: reset-once + static-config-cache + packet-preloaded + input-reuse.

## Expected Outputs

- Batch serial log: `logs/s05_6_batch_benchmark.txt`
- Batch CSV: `logs/s05_6_batch_benchmark.csv`
- Proxy serial log section: `logs/s05_6_batch_benchmark.txt`
- Proxy CSV: `reports/s05_6_proxy_benchmark.csv`

## Results

- Board log: `logs/s05_6_batch_benchmark.txt`
- Batch CSV: `logs/s05_6_batch_benchmark.csv`
- Proxy CSV: `reports/s05_6_proxy_benchmark.csv`
- Board compile: PASS with on-board `gcc`.
- Kernel panic/oops/bus error: none observed.
- AXI-Lite bulk data path used: no.
- AXI DMA MM2S/S2MM used: yes.
- input BRAM used: yes.
- `/dev/mem O_SYNC` carveout used: yes.
- Active BOOT/bitstream/XSA were not regenerated or overwritten.

### Batch Results

The fake_gemv 128-bit path still passes repeated mode=0/mode=1 runs.

| variant | mode | batch | avg/job | fail_count |
|---|---:|---:|---:|---:|
| A reset-every-job | 0 | 1 | 70 us | 0 |
| A reset-every-job | 0 | 256 | 48 us | 0 |
| A reset-every-job | 1 | 1 | 50 us | 0 |
| A reset-every-job | 1 | 256 | 48 us | 0 |
| F combined-hot-path | 0 | 1 | 67 us | 0 |
| F combined-hot-path | 0 | 256 | 27 us | 0 |
| F combined-hot-path | 1 | 1 | 67 us | 0 |
| F combined-hot-path | 1 | 256 | 27 us | 0 |

Selected hot path for fake_gemv batching:

- reset-once
- static-config-cache
- packet-preloaded
- input-reuse
- bounded busy poll

For the small fake_gemv case, batch amortization reduces average per-job latency from about `48 us` in reset-every-job batch256 to about `27 us` in the combined hot path batch256.

### DMA Length Finding

The active AXI DMA is configured with:

- `C_SG_LENGTH_WIDTH=14`
- max simple transfer length: `16383` bytes

This is visible in the active generated hardware metadata:

- `hw/vivado_project/GPTalk.srcs/sources_1/bd/design_1/ip/design_1_axi_dma_0_0/design_1_axi_dma_0_0.xci`
- `hw/vivado_project/GPTalk.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh`

S05.4 real-workload packets are much larger:

- MLP projection proxy `576x1536`: `995328` packet bytes
- down projection proxy `1536x576`: `995328` packet bytes
- lm_head `576x256` chunk proxy: `165888` packet bytes

Therefore full S05.4 tensor packets cannot be sent as one simple-mode MM2S transfer with the current bitstream. A single full-packet attempt causes an early DMA TLAST and GEMV `ERROR_CODE=2`.

S05.6 changed the proxy runner to use TLAST-safe chunks under the 16 KiB DMA limit.

### Proxy Results

| proxy | chunk jobs/tensor | full packet bytes | actual packet bytes | fail_count | result |
|---|---:|---:|---:|---:|---|
| lane_probe_32x16 | 1 | 576 | 576 | 0 | PASS |
| mlp_576x1536_chunked | 96 | 995328 | 995328 | 1 | FAIL |
| down_1536x576_chunked | 108 | 995328 | 995328 | 1 | FAIL |
| lm_head_576x256_chunked | 16 | 165888 | 165888 | 1 | FAIL |

Failure examples from the board log:

- `mlp_576x1536_chunked_chunk_576x16 mode=0 result[12] got=-2147483648 expected=220`
- `down_1536x576_chunked_chunk_512x16 mode=0 result[13] got=39 expected=37`
- `lm_head_576x256_chunked_chunk_576x16 mode=0 result[8] got=2147483647 expected=152`

Interpretation:

- `lane_probe_32x16` proves 128-bit one-block, 16-lane mode=0 can pass.
- S05.5 fake_gemv used `in_features=32`, `out_features=3`, so it did not validate multi-block mode=0 accumulation.
- The real-workload proxy failures appear when `blocks_per_row > 1`.
- The current active 128-bit candidate is not safe for S06 runtime integration.

The immediate suspect is the multi-block mode=0 scale/accumulate path or its input/weight progression under 128-bit streaming. This must be debugged with an RTL/board regression that covers `out_features=16` and `blocks_per_row > 1`.

## S06 Gate

S06 remains blocked. The following criteria are not satisfied because the real-workload proxy failed:

- 128-bit fake_gemv mode=0 batch 100+ PASS.
- 128-bit fake_gemv mode=1 batch 100+ PASS.
- Batch size increase shows lower average per-job latency.
- Hot path has no verbose debug/status reads.
- Reset/config reuse strategy is selected.
- Packet preload strategy is selected.
- S05.4 workload model is updated with S05.6 proxy throughput.

Current S06 decision:

- fake_gemv batching: PASS.
- overhead reduction: PASS for fake_gemv.
- full-packet DMA transfer: blocked by `C_SG_LENGTH_WIDTH=14`.
- chunked real-workload proxy: FAIL.
- S06 entry: NO.

## S05.6.1 Follow-Up

S05.6.1 is now split into `docs/s05_6_1_multiblock_correctness.md`.

- Deterministic RTL regression: PASS.
- Active S05.5 board mini regression: FAIL in three diagnostic cases.
- Candidate signed variable-index RTL fix: built at 74 MHz with timing PASS.
- Candidate BOOT: `artifacts/boot_tests/test_s05_6_1_multiblock_signed_74mhz_s03_fsbl_s03_uboot/BOOT.BIN`
- Candidate BOOT sha256: `c70acfe5c66c6b8f43157723f6a9af667a19d39cd5849e89ad2c2327337c4a2a`
- Board validation: pending SD staging and reboot.

Next required work:

- Stage and boot the S05.6.1 candidate BOOT without overwriting the S05.5 backup artifacts.
- Run board regression for `32x16`, `64x16`, `96x16`, `512x16`, and `576x16`.
- If board PASS, rerun S05.5 fake_gemv and S05.6 batching/proxy checks.
- Increase AXI DMA length width or design a chunk/descriptor contract before treating S05.4 full tensor throughput as representative.
