# Active 상태판

`docs/00_ACTIVE_KR.md`는 현재 상태판이다. 긴 분석, 과거 실패 이력, 프롬프트 전문, 빌드 로그는 여기에 넣지 않는다. 상세는 `docs/s05_*`, `docs/internal/*`, `logs/*`, `reports/*`, `artifacts/boot_tests/*/MANIFEST.txt`를 본다.

## 현재 상태

- 현재 단계: S05.5 128-bit AXIS MM2S bring-up PASS.
- 다음 단계: 사용자 명시 요청 전까지 S05.6 또는 S06으로 진행 금지.
- Active Vivado project: `hw/vivado_project/GPTalk.xpr`
- Vivado GUI에서 열 파일: `hw/vivado_project/GPTalk.xpr`
- 현재 보드 부팅 기준: S05.5 128-bit candidate BOOT.
- 32-bit known-good `GPTalk_dma.bit` / `GPTalk_dma.xsa` alias는 보존됨.
- PetaLinux full rebuild 대상 아님.

## 다음 명령

사용자가 S05.6을 명시 요청하면 먼저 프롬프트를 확인한다.

```bash
sed -n '/# S05.6/,/# S06/p' prompts/smollm2_zybo_guide_v6r3_20260701.md
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
- SD bootfs 최근 device: `/dev/sdb1`
- bootargs: `mem=960M`
- DMA carveout: `0x3c000000-0x3fffffff` (64 MiB), `/dev/mem O_SYNC`
- UIO expected: `axi_dma`, `input_bram`, `gemv_ctrl`
- HDMI Linux console 없음. S04/S05/S06은 serial 기준으로 진행.

## 현재 BOOT/bitstream

- BOOT folder: `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot`
- BOOT.BIN hash: `4d7f875198fed7806b6265db126909067587ee42ec3e5db32308fd62dbd59a8c`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.bit`
- Bitstream hash: `1f873bc39b48d56275f9f07e7fd5db1b979adc118beff768c0b24a9204a7d4ac`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.xsa`
- XSA hash: `005b3f9b04cc521fca0c4979d77a8c19cdab9c0335a477cb973e48a7ec627f08`
- Timing: 74 MHz PASS, setup WNS `0.389 ns`, hold WHS `0.018 ns`
- Timing report: `reports/s05_5_timing_74MHz.rpt`

## S05.5 검증 결과

- `GEMV BUILD_CONFIG=0x00800010 axis_width=128 lanes=16`
- mode=0 scaled PASS: `[-48, 19, -6]`
- mode=1 block_acc PASS: `[-193, 38, -50]`
- quiet 100-run PASS: mode0/mode1 fail_count `0`
- quiet latency: mode0 avg `251 us`, mode1 avg `251 us`
- Root cause fixed: BRAM Port B `INPUT_BRAM_DOUT` floating/module_ref driver value issue. Scalar pin connection으로 수정됨.

## 보존된 32-bit known-good

- Bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Bitstream hash: `158ba9de633fc8ea4a8b4822d0589fad427ec25502ff67cc8d44ef604696acb0`
- XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- XSA hash: `f6ef5281f7558d21435127cc6858e72d312089e1f7a26bed34ce4206ae9a1d7d`
- Known-good 32-bit BOOT hash: `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`
- Backup: `artifacts/s05_3_validated_known_good_reexport_20260701_010926`

## 금지사항

- 사용자 요청 없이 S05.6/S06 진행 금지.
- known-good 32-bit alias 덮어쓰기 금지.
- rejected S05.2 counter bitstream/BOOT 재사용 금지.
- custom/recovery FSBL 사용 금지.
- PetaLinux full rebuild 금지.
- AXI-Lite bulk data path 복귀 금지.
- TLAST/TKEEP check 제거 금지.
- `valid_lanes_reg` output emit fix 되돌리기 금지.

## 참고

- S05.5 상세: `docs/s05_5_128bit_axis_bringup.md`
- S05.4 workload model: `docs/s05_4_real_workload_throughput_model.md`
- S05.3 latency forensic: `docs/s05_3_control_polling_dma_latency.md`
- S05 fake_gemv PASS 기준: `docs/s05_fake_gemv_hw_pass_verify.md`
- DMA buffer provider: `docs/s04_5_dma_buffer_provider.md`
- Vivado GUI: `docs/VIVADO_GUI_KR.md`
- DMA 계약: `docs/internal/interface_contract_dma.md`
- 격리된 과거/실패 산출물: `deprecated/quarantine_20260701_s05_5_cleanup/`
- Git/절대경로 정리 격리: `deprecated/20260701_git_abs_cleanup/`
