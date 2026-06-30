# Active 상태판

## 현재 단계

- 현재 단계: S03 부트 복구 PASS. PetaLinux full rebuild 금지 유지. canonical `test_c_s03_fsbl_active_bit_s03_uboot`에서 DONE LED on, UART root prompt, UIO 노드 확인 완료. 이 BOOT.BIN을 S03 boot recovery baseline으로 채택
- Active Vivado project: `hw/vivado_project/GPTalk.xpr`
- Vivado GUI에서 열 파일: `hw/vivado_project/GPTalk.xpr`

## Active build script

- BD 생성/갱신: 현재 BLOCKED. `scripts/create_or_update_gptalk_dma_bd.tcl`과 `scripts/create_or_update_gptalk_dma_hdmi_bd.tcl`은 PS7 삭제/재생성 경로라 실행 즉시 중단됨
- S02 bitstream/XSA build: `scripts/build_gptalk_dma_bitstream.tcl`
- 실패 report 수집: `scripts/report_failed_impl.tcl`

## 다음에 실행할 명령

하드웨어 preflight 결과를 먼저 확인한다.

```bash
cat logs/hw_preflight_result.txt
cat logs/bootgen_bif.txt
cat logs/fsbl_compare.txt
cat logs/hw_direct_program_result.txt
cat logs/bootgen_bif_s03.txt
cat logs/bootgen_bif_s04.txt   # legacy filename; recovery BOOT BIF 기록
```

다음 단계는 GEMV DMA runtime 접근 확인이다. 이미 boot recovery baseline으로 Linux root prompt와 UIO 노드는 확인했다.

최근 관측 SD 상태:

- SD device: `/dev/sdc`, bootfs `/dev/sdc1`, rootfs `/dev/sdc2`
- 현재 `lsblk` 기준 bootfs/rootfs는 미마운트 상태
- no-bitstream BOOT 테스트 전에는 반드시 `lsblk`로 장치명을 재확인한다
- 현재 SD bootfs active `BOOT.BIN`: `artifacts/boot_tests/test_c_s03_fsbl_active_bit_s03_uboot/BOOT.BIN`
- 현재 SD bootfs `BOOT.BIN` hash: `03b92ed1440d22a3e8dd08e318cc5e9a1f4c2a0a477ab1d1d2e6e113bdb95030`
- SD bootfs backup: `BOOT_BEFORE_TEST_C_CANONICAL_20260630.BIN`
- SD bootfs fsck after staging: clean
- SD bootfs `image.ub`: recovery minimal DT FIT 유지
- rootfs payload: `/opt/smollm2_zybo`
- rootfs payload 내용: recovery `BOOT.BIN`/`image.ub`/minimal DT, current `GPTalk_dma.bit`/`.xsa`, `runtime_c`, fake_gemv golden, Q8_0 model, q8_0_lane16 layout, S03 docs/logs/address map
- rootfs 여유 공간: 약 `4.7G`

보드가 부팅되면 먼저 다음을 확인한다.

```bash
ls -R /opt/smollm2_zybo | head
ls /sys/class/uio
dmesg | grep -Ei 'uio|dma|gemv|panic|ov5640|mipi|xilinx_drm'
```

S02를 재현 빌드할 때만 다음 명령을 사용한다.

```bash
env GPTALK_PL_CLK_MHZ=75 GPTALK_PL_ACTUAL_FREQ_HZ=76923080 \
  /tools/Xilinx/Vivado/2024.2/bin/vivado -mode batch \
  -source scripts/build_gptalk_dma_bitstream.tcl \
  > logs/gptalk_dma_build_75mhz.log 2>&1
```

S03 시작 전 확인:

```bash
ls -lh hw/vivado_project/export/GPTalk_dma.bit hw/vivado_project/export/GPTalk_dma.xsa
```

## 현재 bitstream/XSA

- GPTalk DMA bitstream: `hw/vivado_project/export/GPTalk_dma.bit`
- GPTalk DMA XSA: `hw/vivado_project/export/GPTalk_dma.xsa`
- XSA 내부 bitstream: `GPTalk_dma_74MHz.bit`, active bitstream hash와 일치
- 최고 no-violation 적용 클럭: actual `76.929 MHz` (`FREQ_HZ=76923080`)
- 보존된 75 MHz 산출물: `hw/vivado_project/export/GPTalk_dma_75MHz.bit`, `hw/vivado_project/export/GPTalk_dma_75MHz.xsa`
- Vivado strategy 기록 위치: `logs/vivado_impl_strategy.txt`

## Active RTL

- `vivado_ip/rtl/gemv_q8_0_stream_core.v`
- `vivado_ip/rtl/gemv_q8_0_dma_top.v`
- `vivado_ip/rtl/gemv_q8_0_ctrl_axi_lite.v`

## Deprecated project

- `deprecated/vivado_projects/zybo_gemv_dma/zybo_gemv_dma.xpr`
- `deprecated/vivado_projects/zybo_gemv_smoke/zybo_gemv_smoke.xpr`
- `deprecated/vivado_projects/zybo_gemv_bringup/zybo_gemv_bringup.xpr`

`hw/` 아래 active `.xpr`는 `hw/vivado_project/GPTalk.xpr` 하나만 유지한다.

## 절대 사용 금지

- AXI-Lite `INPUT_DATA` 반복 write로 input vector 전송
- AXI-Lite `STREAM_DATA` 반복 write로 weight/scale stream 전송
- AXI-Lite `RESULT_DATA` 반복 read로 output vector 전송
- smoke register-only bitstream을 full GEMV bitstream으로 취급
- `gemv_q8_0_axi_lite.v`, `gemv_q8_0_axi_lite_smoke.v`를 active datapath로 복구
- mode=0 scaled output 제거
- mode=1 block_acc debug 제거
- lane 수 축소
- fake_gemv 전용 하드코딩 IP

## 마지막 PASS/FAIL 요약

- `scripts/run_gemv_sim.tcl`: PASS
- `scripts/create_or_update_gptalk_dma_bd.tcl`: BLOCKED, 기존 버전은 `design_1`/PS7을 삭제 후 재생성하므로 사용 금지
- GPTalk 내부 BD validate: PASS
- GPTalk top: `design_1_wrapper`
- Address map: `logs/hw_dma_address_map.txt`
- S02 synthesis/implementation/bitstream/XSA: PASS
- S02 최고 클럭 예측/적용: PASS, actual `76.929 MHz`
- Timing summary: setup WNS `0.000 ns`, setup TNS `0.000 ns`, hold WHS `0.025 ns`, hold THS `0.000 ns`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`
- Clock prediction log: `logs/s02_clock_prediction.txt`
- S03 bootgen fallback SD packaging: PASS
- S03 board boot with fallback image: FAIL, rootfs mount 이후 old demo DT/PL mismatch로 kernel panic
- Recovery minimal Linux DT FIT: STAGED, `artifacts/s04_linux_dt/bootfs/image.ub`
- Recovery current-PS-init FSBL build: PASS, `artifacts/s04_linux_dt/zynq_fsbl_gptalk_2024.2.elf`
- Recovery BOOT.BIN bootgen: PASS, `artifacts/s04_linux_dt/bootfs/BOOT.BIN`
- S03 recovery SD bootfs copy: PASS, `/run/media/pjs/bootfs/BOOT.BIN` and `/run/media/pjs/bootfs/image.ub` hashes match staging
- S03 and later SD rootfs payload copy: PASS, `/tmp/sd_rootfs/opt/smollm2_zybo`
- S03 recovery custom BOOT board result: FAIL, DONE LED off
- Previous S03 BOOT board result: DONE LED on, but old DT path kernel panic
- Hardware preflight BD validate: PASS, `logs/hw_preflight_bd_validate.log`
- Hardware preflight STOP reason: BD creation scripts recreated PS7 with board preset instead of proving original GPTalk PS7 preservation; custom/recovery FSBL/BOOT packaging gives DONE LED off
- GEMV HP port policy: GEMV DMA는 PS HP DDR port를 써야 하며, 현재 HDMI+DMA 구조에서는 video VDMA가 HP0, GEMV DMA가 HP1을 사용한다. HP1 사용 자체는 실패 원인이 아니다
- Direct active bitstream Vivado/JTAG program: PASS by Vivado, `logs/hw_direct_program_result.txt`; 물리 DONE LED 확인 필요
- custom/recovery FSBL no-bitstream board result: FAIL, UART 90초 캡처 0바이트, Enter probe 0바이트. Logs: `logs/serial_s04_nobit_20260630_112020.log`, `logs/serial_s04_nobit_enter_probe_20260630_112333.log`
- known-good S03 FSBL no-bitstream board result: PASS, `/dev/ttyUSB1` UART 25849 bytes, U-Boot reached, recovery `image.ub` booted to `root@Zybo-Z7-20:~#`. Logs: `logs/serial_test_b_s03_fsbl_no_bit_ttyUSB1_20260630_113716.log`
- Board shutdown after test_b: PASS, `logs/serial_poweroff_after_test_b_20260630_113956.log`, `reboot: System halted`
- canonical S03 boot recovery with bitstream result: PASS. Test `test_c_s03_fsbl_active_bit_s03_uboot`, BOOT hash `03b92ed1440d22a3e8dd08e318cc5e9a1f4c2a0a477ab1d1d2e6e113bdb95030`, DONE LED on, `/dev/ttyUSB1` prompt observed, Linux root prompt reached. Result log: `logs/test_c_s03_fsbl_active_bit_s03_uboot_result.txt`
- Runtime/DT probe after boot: PASS for basic access. UIO nodes present: `axi_dma`, `input_bram`, `hdmi_vdma`, `hdmi_vtc`, `hdmi_dynclk`, `gemv_ctrl`; `/opt/smollm2_zybo` present. Probe log: `logs/serial_test_c_canonical_runtime_probe_20260630_115317.log`
- boot test folders: `artifacts/boot_tests/test_a_recovery_fsbl_no_bit`, `artifacts/boot_tests/test_b_s03_fsbl_no_bit`, `artifacts/boot_tests/test_c_s03_fsbl_with_bit`, `artifacts/boot_tests/test_d_recovery_fsbl_with_bit`
- Deprecated/hold boot test: `artifacts/boot_tests/test_c_s03_fsbl_with_bit` is DO_NOT_USE because it copied bitstream/U-Boot from recovery artifact paths.
- Active S03 boot recovery baseline: `artifacts/boot_tests/test_c_s03_fsbl_active_bit_s03_uboot/BOOT.BIN`
- BOOT BIF split logs: `logs/bootgen_bif_s03.txt`, `logs/bootgen_bif_s04.txt` (legacy filename for recovery BOOT)
- PetaLinux full rebuild: BLOCKED until hardware/FSBL/BOOT packaging is resolved

## 현재 host tool 상태

- PetaLinux command: not in PATH
- Host ARM Linux cross compiler: not in PATH
- Vivado/bootgen/dtc: `/tools/Xilinx/Vivado/2024.2/bin`
- SD rootfs에는 ARM native `gcc`/`make`가 있음. S05 `runtime_c`는 보드 부팅 후 `/opt/smollm2_zybo/runtime_c`에서 빌드한다.

## 사람이 볼 문서

- `README.md`
- `docs/VIVADO_GUI_KR.md`

## 내부 참고

- `docs/internal/interface_contract_dma.md`: C/RTL/DMA register 계약서
- `docs/internal/hw_dma_architecture.md`: DMA 구조 상세
- `docs/internal/hw_route_recovery.md`: timing/routing 복구 메모
- `prompts/`: Codex/agent용 단계 문서

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 11:02:39 KST
- PL clock target: `50.000 MHz`
- Bitstream: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_50MHz.bit`
- XSA: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_50MHz.xsa`
- Latest bitstream alias: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `1.427` ns, hold WHS `0.016` ns
- Strategy log: `/home/pjs/Desktop/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/home/pjs/Desktop/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 11:23:04 KST
- PL clock target: `75.000 MHz`
- PL clock actual FREQ_HZ: `76923080`
- Bitstream: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_75MHz.bit`
- XSA: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_75MHz.xsa`
- Latest bitstream alias: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `/home/pjs/Desktop/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.000` ns, hold WHS `0.025` ns
- Strategy log: `/home/pjs/Desktop/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/home/pjs/Desktop/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 15:44:23 KST
- PL clock target: `50.000 MHz`
- PL clock actual FREQ_HZ: `50000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_50MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_50MHz.xsa`
- Latest bitstream alias: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.995` ns, hold WHS `0.009` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 15:53:20 KST
- PL clock target: `50.000 MHz`
- PL clock actual FREQ_HZ: `50000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_50MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_50MHz.xsa`
- Latest bitstream alias: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.619` ns, hold WHS `0.020` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 17:16:34 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `/run/media/pjs/6A43-DC8A/smollm2-zybo/hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.033` ns, hold WHS `0.013` ns
- Strategy log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/vivado_impl_strategy.txt`
- S02 verify log: `/run/media/pjs/6A43-DC8A/smollm2-zybo/logs/s02_bitstream_xsa_verify.txt`
