# Active 상태판

`docs/00_ACTIVE_KR.md`는 현재 상태판이다. 긴 분석, 과거 실패 이력, 프롬프트 전문, 빌드 로그는 여기에 넣지 않는다. 상세는 `docs/s05_*`, `docs/internal/*`, `logs/*`, `reports/*`, `artifacts/boot_tests/*/MANIFEST.txt`를 본다.

## 현재 상태

- 현재 단계: S05.6.1 multi-block mode=0 forensic 진행 중.
- 다음 단계: 50MHz timing-isolation 후보로 보드 부팅 후 S05.6.1 board regression을 실행.
- Active Vivado project: `hw/vivado_project/GPTalk.xpr`
- Vivado GUI에서 열 파일: `hw/vivado_project/GPTalk.xpr`
- 현재 SD bootfs: S05.6.1 `sat_pipeline_50MHz` 후보 BOOT 적용 완료.
- 74MHz `sat_pipeline` 후보는 board에서 `E_576x16_P6` 간헐 FAIL로 active 승격 금지.
- 32-bit known-good `GPTalk_dma.bit` / `GPTalk_dma.xsa` alias는 보존됨.
- PetaLinux full rebuild 대상 아님.

## 다음 명령

S05.6.1 결과 확인:

```bash
sed -n '1,220p' docs/s05_6_1_multiblock_correctness.md
```

S05.5 보드 상태만 재확인할 때:

```bash
env S05_SERIAL=/dev/ttyUSB1 \
  S05_LOG_PATH=logs/s05_5_axis128_bram_scalar_board_quiet_100_rerun.txt \
  S05_EXTRA_ARGS="--expect-axis-width 128 --quiet-pass --repeat 100 --poll-sleep-us 0" \
  S05_RUN_TIMEOUT=240 \
  python3 scripts/s05_gemv_hw_test.py
```

SD 작업 전:

```bash
lsblk -o NAME,SIZE,FSTYPE,LABEL,UUID,MOUNTPOINTS
```

## 현재 보드/SD

- Serial console: `/dev/ttyUSB1`, `115200`
- SD bootfs 최근 device: `/dev/sdc1`
- bootargs: `mem=960M`
- DMA carveout: `0x3c000000-0x3fffffff` (64 MiB), `/dev/mem O_SYNC`
- UIO expected: `axi_dma`, `input_bram`, `gemv_ctrl`
- HDMI Linux console 없음. S04/S05/S06은 serial 기준으로 진행.

## 현재 BOOT/bitstream

### Active board baseline

- BOOT folder: `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot`
- BOOT.BIN hash: `4d7f875198fed7806b6265db126909067587ee42ec3e5db32308fd62dbd59a8c`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.bit`
- Bitstream hash: `1f873bc39b48d56275f9f07e7fd5db1b979adc118beff768c0b24a9204a7d4ac`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.xsa`
- XSA hash: `005b3f9b04cc521fca0c4979d77a8c19cdab9c0335a477cb973e48a7ec627f08`
- Timing: 74 MHz PASS, setup WNS `0.389 ns`, hold WHS `0.018 ns`
- Timing report: `reports/s05_5_timing_74MHz.rpt`

### S05.6.1 current SD candidate, not yet active

- BOOT folder: `artifacts/boot_tests/test_s05_6_1_sat_pipeline_50mhz_s03_fsbl_s03_uboot`
- BOOT.BIN hash: `81462f69352a0ca0f10c823ae175010acc86ad209ecf238c96596032108ea305`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_1_sat_pipeline_50MHz.bit`
- Bitstream hash: `4aa46386c69477f08cca716845e4ca7ef4244c425f9533ad7bb90e61db55b00a`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_1_sat_pipeline_50MHz.xsa`
- XSA hash: `a2fbbbb6226d81a371cade5f3e89bfde0f06a7defccd47395da3a52ef2760f09`
- Timing: 50 MHz PASS, setup WNS `0.928 ns`, hold WHS `0.016 ns`
- SD staging: `/dev/sdc1:/BOOT.BIN` hash verified, previous BOOT backed up as `BOOT_BEFORE_TEST_S05_6_1_SAT_PIPELINE_50MHZ_S03_FSBL_S03_UBOOT_20260701_183243.BIN`
- SD health before/after staging: `bootfs` dirty bit repaired; post-stage `fsck.vfat -n /dev/sdc1` clean. `rootfs` read-only `e2fsck -fn /dev/sdc2` clean.
- Board status: 검증 대기.

### S05.6.1 rejected/intermediate candidate

- BOOT folder: `artifacts/boot_tests/test_s05_6_1_multiblock_signed_74mhz_s03_fsbl_s03_uboot`
- BOOT.BIN hash: `c70acfe5c66c6b8f43157723f6a9af667a19d39cd5849e89ad2c2327337c4a2a`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_1_multiblock_signed_74MHz.bit`
- Bitstream hash: `b27c6dcc1e408633883a3d22ea42fdc939fcd858cd79f87caa22dd80f55aa3d5`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_1_multiblock_signed_74MHz.xsa`
- XSA hash: `d27b65193bc6f09c867eb5a73f22a2ae2d06b88b0d45b200fa2764cc5e3eead9`
- Timing: 74 MHz PASS, setup WNS `0.252 ns`, hold WHS `0.016 ns`
- RTL sim: `logs/s05_6_1_multiblock_rtl_sim.txt` PASS
- Board status: rejected, board mini regression 및 S05.5 fake regression 실패 이력 있음.

## S05.5 검증 결과

- `GEMV BUILD_CONFIG=0x00800010 axis_width=128 lanes=16`
- mode=0 scaled PASS: `[-48, 19, -6]`
- mode=1 block_acc PASS: `[-193, 38, -50]`
- quiet 100-run PASS: mode0/mode1 fail_count `0`
- quiet latency: mode0 avg `251 us`, mode1 avg `251 us`
- Root cause fixed: BRAM Port B `INPUT_BRAM_DOUT` floating/module_ref driver value issue. Scalar pin connection으로 수정됨.

## S05.6 결과

- 상세: `docs/s05_6_batching_persistent_job.md`
- Batch log: `logs/s05_6_batch_benchmark.txt`
- Batch CSV: `logs/s05_6_batch_benchmark.csv`
- Proxy CSV: `reports/s05_6_proxy_benchmark.csv`
- fake_gemv 128-bit batch: mode0/mode1 fail_count `0`
- `F_combined_hot_path` batch256: mode0 avg `27 us`, mode1 avg `27 us`
- AXI DMA simple length width: `14`, max single transfer `16383` bytes
- S05.4 full proxy packet `995328` bytes는 현재 single DMA transfer 불가. TLAST-safe chunking 필요.
- `lane_probe_32x16` PASS, but multi-block mode=0 proxy FAIL:
  - `mlp_576x1536_chunked`: FAIL
  - `down_1536x576_chunked`: FAIL
  - `lm_head_576x256_chunked`: FAIL
- 판정: S06 진입 금지. S05.5 fake_gemv PASS는 real SmolLM2 multi-block mode=0 correctness를 보장하지 않음.

## S05.6.1 결과

- 상세: `docs/s05_6_1_multiblock_correctness.md`
- Reference/golden: `golden/s05_6_1_multiblock/`
- RTL sim: PASS
- Active S05.5 board mini regression: FAIL 3개
  - `B_64x16_P4`: `INPUT_ADDR_PROGRESSION_FAIL`
  - `B_64x16_P5`: `WEIGHT_BLOCK_PROGRESSION_FAIL`
  - `E_576x16_P3`: `SATURATION_FAIL`
- RTL 수정: signed variable-index read와 final emit saturation을 명시화.
- 74MHz `sat_pipeline` 후보: RTL sim PASS, S05.6.1 기본 board regression PASS, S05.5 fake 100-run PASS. 하지만 proxy pattern `E_576x16_P6`가 보드에서 간헐 `INT_MAX/INT_MIN` FAIL이라 active 승격 금지.
- 50MHz `sat_pipeline` 후보: SD 적용 완료, board 검증 대기.

## 보존된 32-bit known-good

- Bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Bitstream hash: `158ba9de633fc8ea4a8b4822d0589fad427ec25502ff67cc8d44ef604696acb0`
- XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- XSA hash: `f6ef5281f7558d21435127cc6858e72d312089e1f7a26bed34ce4206ae9a1d7d`
- Known-good 32-bit BOOT hash: `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`
- Backup: `artifacts/s05_3_validated_known_good_reexport_20260701_010926`

## 금지사항

- 사용자 요청 없이 S06 진행 금지.
- S05.6 multi-block mode=0 proxy FAIL을 무시하고 runtime 연결 금지.
- known-good 32-bit alias 덮어쓰기 금지.
- active S05.5 128-bit BOOT/bitstream 덮어쓰기 금지.
- S05.6.1 후보를 board PASS 전 active로 승격 금지.
- rejected S05.2 counter bitstream/BOOT 재사용 금지.
- custom/recovery FSBL 사용 금지.
- PetaLinux full rebuild 금지.
- AXI-Lite bulk data path 복귀 금지.
- TLAST/TKEEP check 제거 금지.
- `valid_lanes_reg` output emit fix 되돌리기 금지.

## 참고

- S05.5 상세: `docs/s05_5_128bit_axis_bringup.md`
- S05.6 상세: `docs/s05_6_batching_persistent_job.md`
- S05.6.1 상세: `docs/s05_6_1_multiblock_correctness.md`
- S05.4 workload model: `docs/s05_4_real_workload_throughput_model.md`
- S05.3 latency forensic: `docs/s05_3_control_polling_dma_latency.md`
- S05 fake_gemv PASS 기준: `docs/s05_fake_gemv_hw_pass_verify.md`
- DMA buffer provider: `docs/s04_5_dma_buffer_provider.md`
- Vivado GUI: `docs/VIVADO_GUI_KR.md`
- DMA 계약: `docs/internal/interface_contract_dma.md`
- 격리된 과거/실패 산출물: `deprecated/quarantine_20260701_s05_5_cleanup/`
- Git/절대경로 정리 격리: `deprecated/20260701_git_abs_cleanup/`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 17:23:09 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `71428566`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_rowout_signed_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_rowout_signed_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.389` ns, hold WHS `0.018` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 17:31:23 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `71428566`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_muxsat_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_muxsat_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.061` ns, hold WHS `0.050` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 17:46:38 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `71428566`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_min_sat_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_min_sat_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.369` ns, hold WHS `0.017` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 17:58:06 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `71428566`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_inline_sat_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_inline_sat_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.369` ns, hold WHS `0.017` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 18:05:14 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `71428566`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_sat_pipeline_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_sat_pipeline_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.270` ns, hold WHS `0.015` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 18:26:26 KST
- PL clock target: `50.000 MHz`
- PL clock actual FREQ_HZ: `50000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_sat_pipeline_50MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_sat_pipeline_50MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.928` ns, hold WHS `0.016` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 22:14:34 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_scale_operand_pipeline_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_scale_operand_pipeline_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.760` ns, hold WHS `0.015` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 22:29:03 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_state_encoding_preserve_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_1_state_encoding_preserve_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.564` ns, hold WHS `0.016` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 05:16:09 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_fixed_128_mac_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_fixed_128_mac_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.466` ns, hold WHS `0.015` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 05:37:59 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_snapshot_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_snapshot_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.542` ns, hold WHS `0.005` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 05:59:02 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_narrow_debug_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_narrow_debug_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.914` ns, hold WHS `0.010` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 06:18:04 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_selected_debug_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_selected_debug_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.569` ns, hold WHS `0.009` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 06:34:46 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_staged_emit_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_staged_emit_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.794` ns, hold WHS `0.018` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 06:49:32 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_final_apply_snapshot_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_final_apply_snapshot_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.832` ns, hold WHS `0.010` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 07:02:11 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_scalar_snapshot_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_scalar_snapshot_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.364` ns, hold WHS `0.018` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 07:16:14 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_dma_top_output_register_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_dma_top_output_register_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.758` ns, hold WHS `0.013` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 07:32:33 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_load_stage_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_emit_load_stage_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.632` ns, hold WHS `0.006` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 07:47:56 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_reuse_scale_pipeline_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_reuse_scale_pipeline_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.838` ns, hold WHS `0.008` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-02 08:06:15 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.xsa`
- Latest bitstream alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Latest XSA alias: `SKIPPED_BY_GPTALK_UPDATE_LATEST=0`
- Timing: setup WNS `0.591` ns, hold WHS `0.008` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`
