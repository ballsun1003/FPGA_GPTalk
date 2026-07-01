# Active 상태판

## 현재 단계

- 현재 단계: S05 PASS 완료, S05.2 성능 측정 완료. RTL performance counter bitstream은 mode=0 board regression FAIL로 폐기했고, SD BOOT는 known-good S05 BOOT hash `17a771c5...`로 복구했다. C-only host timing은 known-good BOOT에서 2회 PASS. mode=0 scaled와 mode=1 block_acc 모두 PASS, AXI DMA MM2S/S2MM 사용, input BRAM 사용, AXI-Lite bulk path 미사용, `/dev/mem` carveout 사용, kernel panic/oops/bus error 없음. 다음 단계는 S06/S05 이후 통합이며, 우선순위 높은 최적화는 lane 확장보다 job batching/polling overhead 감소다.
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

S05는 전원 재투입 후 보드 재검증까지 PASS했고, S05.2 C-only timing도 PASS했다. SD bootfs `uEnv.txt`의 `mem=960M` carveout은 유지되어야 한다. PetaLinux full rebuild 대상 문제가 아니다. 다음 실작업은 S06/S05 이후 통합 또는 S05.1에서 임시 단순화한 scalar scale pipeline 성능 복구다.

최근 관측 SD 상태:

- SD device: `/dev/sdc`, bootfs `/dev/sdc1`, rootfs `/dev/sdc2`
- 현재 `lsblk` 기준 bootfs/rootfs는 미마운트 상태
- no-bitstream BOOT 테스트 전에는 반드시 `lsblk`로 장치명을 재확인한다
- 현재 SD bootfs active `BOOT.BIN`: `artifacts/boot_tests/test_s05_1_valid_emit_s03_fsbl_active_bit_s03_uboot/BOOT.BIN` (S05.2 실패 BOOT를 보드에서 복구 완료)
- 현재 SD bootfs `BOOT.BIN` hash: `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`
- live board bootfs recheck: `/run/media/mmcblk0p1/BOOT.BIN` hash도 동일, log `logs/s05_live_boot_media_hash.txt`
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

S04.5C에서 검증한 carveout:

- DDR: `0x00000000-0x3fffffff` (1 GiB)
- temporary bootargs: `mem=960M`
- permanent bootargs source: SD bootfs `/uEnv.txt`
- Linux System RAM after S04.5A: `0x00000000-0x3bffffff`
- carveout: `0x3c000000-0x3fffffff` (64 MiB)
- S04.5A/B/C logs/docs: `logs/s04_5a_temp_mem_boot.txt`, `logs/s04_5b_sd_bootargs_update.txt`, `logs/s04_5b_boot_verify.txt`, `logs/s04_5c_dma_carveout_smoke.txt`, `docs/s04_5_dma_buffer_provider.md`

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
- GPTalk DMA XSA: 현재 active known-good XSA alias 없음. S05.2 실패 counter XSA는 `hw/vivado_project/export/GPTalk_dma_s05_2_failed_counter_DO_NOT_USE.xsa`로 격리함
- XSA 내부 bitstream: S05.2 실패 XSA는 사용 금지. 다음 XSA/PetaLinux 흐름 전에는 known-good RTL 기준으로 재export 필요
- 현재 active bitstream hash: `f774ab97d114e4a0cca5df30ca1caa71197c34d109e3d12e830cf0ff83bb233c`
- 현재 active 적용 클럭: `74.000 MHz` (`FREQ_HZ=74000000`)
- 보존된 75 MHz 산출물: `hw/vivado_project/export/GPTalk_dma_75MHz.bit`, `hw/vivado_project/export/GPTalk_dma_75MHz.xsa`; hash `42f8961d9a8f59f1bc37c8d92df52db876137eaa796957d8ef104426ab9ce805`, 현재 보드 image 아님
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
- HDMI no-output diagnostic: EXPECTED with current Linux image. `/dev/fb0` and `/dev/dri/card0` absent; active console is `ttyPS0`; HDMI VDMA/VTC/dynclk are UIO-only. S04/S05/S06 continue over serial. S07 must add HDMI userspace init/draw or framebuffer/DRM path. Log: `logs/hdmi_no_output_diag.txt`
- S04 UIO/register/DMA register smoke: PASS for name-based UIO lookup, address map compare, GEMV register read, input BRAM write/readback, AXI DMA reset/status. No bus error/kernel oops observed. At that time DMA data transfer was blocked by missing buffer provider; S04.5C now provides the MVP `/dev/mem` carveout path. Logs: `logs/s04_board_uio_register_dma_smoke.txt`, `docs/s04_smoke_verify.md`
- S04.5A temporary `mem=960M` carveout boot: PASS. SD 수정 없음, `saveenv` 없음, root prompt reached, `/proc/cmdline` contains `mem=960M`, `/proc/iomem` System RAM is `00000000-3bffffff`, UIO nodes remain present. Candidate carveout for S04.5C: `0x3c000000-0x3fffffff` (64 MiB). Logs/docs: `logs/s04_5a_temp_mem_boot.txt`, `docs/s04_5_dma_buffer_provider.md`
- S04.5C `/dev/mem` carveout smoke: PASS. Board native compile PASS, `/dev/mem O_RDWR|O_SYNC` open PASS, 64 MiB mmap PASS, 64 pattern write/readback PASS from `0x3c000000` through `0x3ffffffc`, run RC `0`, no bus error/kernel oops observed. Logs/source: `logs/s04_5c_dma_carveout_smoke.txt`, `artifacts/boot_tests/s04_5_dma_carveout_smoke.c`, `scripts/s04_5c_carveout_smoke.py`
- S04.5B permanent bootargs: PASS, power-cycle reverify PASS at 2026-06-30 14:50:53 KST. SD bootfs `/uEnv.txt` added with verified `mem=960M` bootargs, backup at bootfs `/backup/bootargs_20260630_144412/`, `BOOT.BIN` and `image.ub` unchanged, reboot verification PASS: `/proc/cmdline` has `mem=960M`, `/proc/iomem` System RAM is `00000000-3bffffff`, UIO nodes present, bootfs `uEnv.txt` sha256 `369ff646c0c07a3ac5343d5dd7f26f5b18ced4ca56a3e7db3d532bdf846f7c76`. Logs: `logs/s04_5b_sd_bootargs_update.txt`, `logs/s04_5b_boot_verify.txt`
- S05 fake_gemv DMA smoke: FAIL, PASS 조건 미충족. 최신 active BOOT hash `03b92ed1440d22a3e8dd08e318cc5e9a1f4c2a0a477ab1d1d2e6e113bdb95030`와 active 74 MHz bitstream hash `0053cee7664da6932bd7b70aef091de1b0569c055b200a188f6bbc2a6b59d681` 기준에서 실행됨. `gemv_hw_test` compile PASS, AXI DMA MM2S/S2MM + input BRAM + `/dev/mem` carveout 사용 확인, AXI-Lite bulk path 미사용. fake_gemv는 16-lane geometry이며 `out_features=3`, `padded_out_features=16`, `row_padding=13`. mode=0/mode=1 모두 GEMV `ERROR_CODE=2`, MM2S IOC/idle, S2MM no IOC, debug `row=0 block=0 lane=8`; 이 lane=8은 8-lane 설정이 아니라 RTL `weight_lane_base`이며, 정상 TLAST 기대 위치는 `lane_base=12`. Logs/docs: `logs/s05_gemv_hw_test.txt`, `logs/s05_live_boot_media_hash.txt`, `docs/s05_fake_gemv_hw_pass_verify.md`
- S05.1 first board rerun with non-OOC-stale fix BOOT: FAIL. DONE/Linux prompt PASS, but S05 still `ERROR_CODE=2 debug_lane=8`. Diagnosis after rerun: `hw/vivado_project/GPTalk.runs/design_1_gemv_q8_0_dma_top_0_0_synth_1/design_1_gemv_q8_0_dma_top_0_0.dcp` was stale from 2026-06-29, so the first S05.1 BOOT still contained old GEMV RTL despite a new top bitstream hash. Logs: `logs/s05_gemv_hw_test.txt`, `logs/s05_1_build_boot_summary.txt`
- S05.1 OOC-rebuilt RTL fix: build PASS, SD staging PASS, board rerun pending. Build script now resets/rebuilds GEMV module_ref OOC run. OOC DCP timestamp `2026-06-30 17:28:48 KST`, DCP hash `99ddcca7ca30b077df2d1bc30f1d3d4426770696c368e85e6be8a6a79b886947`. New active bitstream hash `9a39b3b321fc93c89c2140b1c98aba14a1e4678622f6e6dc8e2aa9189e220da6`, XSA hash `aafd118979c6635220fcdff84b2b643d18aa726d6678160d11f7a4d89cb84c34`, timing setup WNS `0.001 ns`, hold WHS `0.008 ns`. OOC-rebuilt BOOT staged to SD `/dev/sdb1:/BOOT.BIN`, sha256 `0c6d39192a51b78e2cbd8c711ca9958a21563ead372c464548eac90968a9a720`; previous first S05.1 BOOT backed up as `/dev/sdb1:/BOOT_BEFORE_S05_1_OOC_REBUILT_20260630.BIN`, sha256 `ce5e2939614531a14e6957b17ee9ff6fa3ec7103ca0031a45068e6bd71144b97`. Logs/docs: `logs/s05_1_gptalk_dma_rebuild_ooc.log`, `logs/s05_1_bootgen_ooc_handshake_boot.log`, `logs/s05_1_build_boot_summary.txt`, `artifacts/boot_tests/test_s05_1_ooc_rebuilt_s03_fsbl_handshake_bit_s03_uboot/MANIFEST.txt`, `docs/s05_1_tlast_contract_debug.md`
- S05.1 OOC-rebuilt board rerun: FAIL with numerical mismatch, not TLAST. `ERROR_CODE=0`, MM2S/S2MM IOC/idle, GEMV done. mode=0 got `[513, 20, 0]` expected `[-48, 19, -6]`; mode=1 got `[35499572, -771350760, 0]` expected `[-193, 38, -50]`. This moved the active suspect to input BRAM read latency/packing. Log archived: `logs/s05_gemv_hw_test_ooc_rebuilt_result_mismatch.txt`
- S05.1 BRAM wait fix: build/SD staging PASS, board rerun moved failure from TLAST to numerical mismatch. RTL change added `ST_INPUT_WAIT2` before `ST_WEIGHT_APPLY`; stream-core sim PASS. OOC DCP hash `70601d7d5e896f4d8f1ada100a873b301ecf531d5a43ce8e94086d9986ccadd7`, bitstream hash `6c67721e66745af9062b64d98c6efe93906a76a09d748c0524ac76ac09eead34`, XSA hash `2a3929303906ff91c3cf14b6d26df9ebabf51ff983f15a12a40413982a25b6f2`, timing setup WNS `0.021 ns`, hold WHS `0.007 ns`. Logs/docs: `logs/s05_1_rtl_bram_wait_tb_vivado.log`, `logs/s05_1_gptalk_dma_rebuild_ooc_bramwait.log`, `logs/s05_1_bootgen_bram_wait_boot.log`, `artifacts/boot_tests/test_s05_1_bram_wait_s03_fsbl_handshake_bit_s03_uboot/MANIFEST.txt`
- S05.1 scalar scale board rerun: FAIL only in mode=0 scaled. BOOT hash `caafb1588997231a72f6365f35a6786cf8b094bfeaef8de334aedd9aff5b10c2`, active bitstream hash `f013fd85702efdb0c9b5c61550609726d3900b3986e2cf9b5138d0a84017d0b3`. DONE/Linux prompt PASS by user report and serial. TLAST normal: `in_count=144`, `tlast_count=143`, `tlast_tkeep=0xf`, `ERROR_CODE=0`. mode=1 block_acc PASS, got `[-193, 38, -50]`. mode=0 scaled FAIL, latest rerun got `[-48, 5139, 3066]` expected `[-48, 19, -6]`; prior rerun got row1 `4115`, so row1 offset is not stable across reboot/rerun. This is now mode=0 scale/round/accumulate forensic work, not S03/S04/boot/PetaLinux. Log: `logs/s05_gemv_hw_test.txt`
- S05.1 scale-debug BOOT: build PASS, SD staging PASS, board rerun pending. Added AXI-Lite debug register readback for mode=0 row/lane 0..2 `scale_q`, `block_acc`, `product`, `scaled`, and `row_acc`. Stream-core sim PASS after instrumentation. Timing PASS setup WNS `0.209 ns`, hold WHS `0.016 ns`. Active bitstream hash `59c936dccfc6b2f268ea4eff884f4987d682ea14be15a08d437b791fc60a608a`, XSA hash `a0e4944d8dd99d48c42663004e48d17fa705b4f02b60328b7719c3bf1094d939`, GEMV OOC DCP hash `f3ec379a71f1bbd8d3f4f786c26c3660d1ffcc4bfc7471603001890fbd611422`. BOOT hash `54baaa510a4a123c4c1e6ec5b66a4756c2101e1efde90c86e317764c34c931ff`; SD `/BOOT.BIN` hash matches. Previous scalar-scale BOOT backup on SD: `/BOOT_BEFORE_TEST_S05_1_SCALE_DEBUG_S03_FSBL_ACTIVE_BIT_S03_UBOOT_20260630_191253.BIN`, sha256 `caafb1588997231a72f6365f35a6786cf8b094bfeaef8de334aedd9aff5b10c2`. Logs/docs: `logs/s05_1_gptalk_dma_rebuild_scale_debug.log`, `logs/test_s05_1_scale_debug_s03_fsbl_active_bit_s03_uboot_bootgen.log`, `logs/test_s05_1_scale_debug_s03_fsbl_active_bit_s03_uboot_sd_stage.log`, `artifacts/boot_tests/test_s05_1_scale_debug_s03_fsbl_active_bit_s03_uboot/MANIFEST.txt`
- S05.1 row_out fix BOOT: build PASS, SD staging PASS, board rerun FAIL. The scale-debug board rerun proved mode=0 internal values were correct: `scale=[262144,524288,131072]`, `block=[-193,38,-50]`, `scaled=[-48,19,-6]`, `row_acc=[-48,19,-6]`; only `debug_out`/DDR emitted results were corrupt. RTL latched saturated 32-bit `row_out[]` at `ST_SCALE_ACCUM` and emitted from `row_out[]` in `ST_EMIT_ROW`, but board reruns still corrupted output beats and mode=1 row2, confirming output emit/S2MM stream sequencing issue rather than scale arithmetic. Timing PASS setup WNS `0.373 ns`, hold WHS `0.012 ns`. BOOT hash `24847d4879b1ed6f93bd0e674df311a3a4cdab0886bacc3d50a8f2350b7dea95`. Logs: `logs/s05_gemv_hw_test_row_out_rerun_1.txt`, `logs/s05_gemv_hw_test.txt`, `logs/s05_1_gptalk_dma_rebuild_row_out.log`, `artifacts/boot_tests/test_s05_1_row_out_s03_fsbl_active_bit_s03_uboot/MANIFEST.txt`
- S05.1 valid_emit BOOT: build PASS, SD staging PASS, board rerun PASS twice and power-cycle rerun PASS once. RTL now emits only valid rows using `valid_lanes_reg` instead of scanning padded lanes 3..15 after the last valid row. First valid_emit implementation had timing WNS `-0.005 ns`, then optimized the last-lane compare by latching `valid_lanes_reg`; final timing PASS setup WNS `0.352 ns`, hold WHS `0.016 ns`. Active bitstream hash `f774ab97d114e4a0cca5df30ca1caa71197c34d109e3d12e830cf0ff83bb233c`, XSA hash `5100566de9bcced4f60d4734e7d0159bde12fcce3594ca36508933db22891987`, GEMV OOC DCP hash `8a65adeb195e07eae803e0f92a1bfeafa183b7d807fb3f89c15d6387906af3bf`. BOOT hash `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`; SD `/BOOT.BIN` hash matches. Previous row_out BOOT backup on SD: `/BOOT_BEFORE_TEST_S05_1_VALID_EMIT_S03_FSBL_ACTIVE_BIT_S03_UBOOT_20260630_194749.BIN`, sha256 `24847d4879b1ed6f93bd0e674df311a3a4cdab0886bacc3d50a8f2350b7dea95`. Board result: mode=0 scaled PASS `[-48, 19, -6]`, mode=1 block_acc PASS `[-193, 38, -50]`, `ERROR_CODE=0`, `in_count=144`, `tlast_count=143`, MM2S/S2MM IOC+idle, no timeout/error/fallback. Power-cycle log `logs/s05_gemv_hw_test_power_cycle_pass_20260630_1952.txt`, sha256 `fbc5a30c8e98036e3223d0f00b383566d9757aaf534d8bdbff128ad2494d9106`. Logs/docs: `logs/s05_gemv_hw_test_valid_emit_pass_1.txt`, `logs/s05_gemv_hw_test_valid_emit_pass_2.txt`, `logs/s05_gemv_hw_test.txt`, `logs/s05_1_valid_emit_latched_tb_vivado.log`, `logs/s05_1_gptalk_dma_rebuild_valid_emit_latched.log`, `logs/test_s05_1_valid_emit_s03_fsbl_active_bit_s03_uboot_bootgen.log`, `logs/test_s05_1_valid_emit_s03_fsbl_active_bit_s03_uboot_sd_stage.log`, `artifacts/boot_tests/test_s05_1_valid_emit_s03_fsbl_active_bit_s03_uboot/MANIFEST.txt`, `docs/s05_fake_gemv_hw_pass_verify.md`
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
- Bitstream: `hw/vivado_project/export/GPTalk_dma_50MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_50MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `1.427` ns, hold WHS `0.016` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 11:23:04 KST
- PL clock target: `75.000 MHz`
- PL clock actual FREQ_HZ: `76923080`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_75MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_75MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.000` ns, hold WHS `0.025` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 15:44:23 KST
- PL clock target: `50.000 MHz`
- PL clock actual FREQ_HZ: `50000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_50MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_50MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.995` ns, hold WHS `0.009` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 15:53:20 KST
- PL clock target: `50.000 MHz`
- PL clock actual FREQ_HZ: `50000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_50MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_50MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.619` ns, hold WHS `0.020` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-29 17:16:34 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.033` ns, hold WHS `0.013` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 17:11:27 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.033` ns, hold WHS `0.013` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 17:35:34 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.001` ns, hold WHS `0.008` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 17:56:49 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.021` ns, hold WHS `0.007` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 18:19:55 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.081` ns, hold WHS `0.007` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S05.1 debug/clear BOOT 패키징 기록

- 기록 시각: 2026-06-30 18:22:02 KST
- 상태: SD 복사 완료, `/dev/sdb1` unmount 완료, 보드 부팅 및 S05 rerun 완료, mode=0 scaled FAIL / mode=1 block_acc PASS
- 목적: `CONTROL.clear`가 core reset에 연결되지 않아 TLAST error 이후 상태가 남는 문제를 수정하고, S05 결과 mismatch 분리를 위해 GEMV debug output readback register를 추가
- RTL sim: PASS, `logs/s05_1_debug_regs_tb_run.log`
- Build log: `logs/s05_1_gptalk_dma_rebuild_debug_regs.log`
- Bootgen log: `logs/s05_1_bootgen_debug_regs_boot.log`
- Active bitstream: `hw/vivado_project/export/GPTalk_dma.bit`
- Active bitstream sha256: `32073aa75fcacbc8b1f6312176d2896ba0d83c2f264a6d31ca1a42ca5c811a9b`
- Active XSA: `hw/vivado_project/export/GPTalk_dma.xsa`
- Active XSA sha256: `086bd947b2cbf9c572c7a33a94918584176017d2a06ff8000ded449ae616c195`
- GEMV OOC DCP sha256: `a6eff71b6ad3ad0ba436e2a32499f8272a7a2f950d351793f0e70432ac67e33a`
- BOOT test folder: `artifacts/boot_tests/test_s05_1_debug_regs_s03_fsbl_active_bit_s03_uboot`
- BOOT.BIN sha256: `4a6536024bb4ee5bd607ff0e1b551430cbd2427b6a397339caa01c9a3cdebb36`
- SD `/BOOT.BIN` sha256: `4a6536024bb4ee5bd607ff0e1b551430cbd2427b6a397339caa01c9a3cdebb36`
- SD 이전 BOOT backup: `/BOOT_BEFORE_S05_1_DEBUG_REGS_20260630.BIN`, sha256 `166e9cd953e70d7772a6b45cdba40b6014928de66ba408d3cd26f9336970a997`
- SD staging log: `logs/s05_1_sd_stage_debug_regs_boot.txt`
- BOOT 구성: known-good S03 FSBL `610d49ef...` + active debug/clear bitstream `32073aa7...` + S03 U-Boot `79a91b6f...`
- 주의: 이후 보드 S05 결과는 SD `/BOOT.BIN` hash `4a653602...` 기준으로 해석한다.

## S05.1 input TLAST debug BOOT 패키징 기록

- 기록 시각: 2026-06-30 18:42:01 KST
- 상태: SD 복사 완료, `/dev/sdb1` unmount 완료, 보드 장착/부팅 대기 중
- 목적: clean boot에서도 `ERROR_CODE=2`가 재현되어, core가 실제로 본 `TLAST` beat/count/data/keep을 AXI-Lite로 읽기 위한 계측 추가
- RTL sim: PASS, `logs/s05_1_input_tlast_debug_tb_run.log`
- Build log: `logs/s05_1_gptalk_dma_rebuild_input_tlast_debug.log`
- Bootgen log: `logs/s05_1_bootgen_input_tlast_debug_boot.log`
- SD staging log: `logs/s05_1_sd_stage_input_tlast_debug_boot.txt`
- Active bitstream: `hw/vivado_project/export/GPTalk_dma.bit`
- Active bitstream sha256: `ea96e91f450f6932b9c85fc0143fa7c0d869a9fd231d2d2690670b1a9e2325db`
- Active XSA sha256: `2cc3f0d67f8c1519f746a9c126aa0276072cc92d41d331d26c3f692941f8218c`
- GEMV OOC DCP sha256: `d8d1f80343a0b7f1d544517c22c22eec1969f513e443fb427a5b723f033e5246`
- Timing: setup WNS `0.175 ns`, hold WHS `0.016 ns`
- BOOT test folder: `artifacts/boot_tests/test_s05_1_input_tlast_debug_s03_fsbl_active_bit_s03_uboot`
- BOOT.BIN sha256: `0f909df23586d376e1a4c48ae7ce9ddb24f25207ab9872e169fba9fdd5248e1f`
- SD `/BOOT.BIN` sha256: `0f909df23586d376e1a4c48ae7ce9ddb24f25207ab9872e169fba9fdd5248e1f`
- SD 이전 BOOT backup: `/BOOT_BEFORE_S05_1_INPUT_TLAST_DEBUG_20260630.BIN`, sha256 `4a6536024bb4ee5bd607ff0e1b551430cbd2427b6a397339caa01c9a3cdebb36`
- BOOT 구성: known-good S03 FSBL `610d49ef...` + active input-TLAST-debug bitstream `ea96e91f...` + S03 U-Boot `79a91b6f...`
- 주의: 이후 보드 S05 결과는 SD `/BOOT.BIN` hash `0f909df2...` 기준으로 해석한다.

## BOOT 패키징 자동화

- 스크립트: `scripts/package_gptalk_boot.py`
- 용도: active `GPTalk_dma.bit`를 known-good S03 FSBL + S03 U-Boot와 함께 BOOT.BIN으로 패키징하고, MANIFEST/hash를 생성하며, 선택 시 SD bootfs에 복사/검증/unmount까지 수행
- 기본 입력: `artifacts/s03_bootgen/zynq_fsbl.elf`, `hw/vivado_project/export/GPTalk_dma.bit`, `artifacts/s03_bootgen/u-boot.elf`
- 사용 예:
  `python3 scripts/package_gptalk_boot.py --test-name test_name --purpose "short reason"`
- SD까지 굽는 예:
  `python3 scripts/package_gptalk_boot.py --test-name test_name --purpose "short reason" --stage-sd /dev/sdb1`
- 주의: Vivado bitstream rebuild는 자동으로 수행하지 않는다. 먼저 `scripts/build_gptalk_dma_bitstream.tcl`이 PASS하고 active bitstream hash를 확인한 뒤 이 스크립트를 사용한다.

## S05.1 scalar scale BOOT 패키징 기록

- 기록 시각: 2026-06-30 18:54:13 KST
- 상태: SD 복사 완료, `/dev/sdb1` unmount 완료, 보드 장착/부팅 대기 중
- 목적: input TLAST 정상 및 mode=1 PASS 확인 후, mode=0 scaled path를 lane별 scalar pipeline으로 단순화
- RTL sim: PASS, `logs/s05_1_scalar_scale_tb_run.log`
- Build log: `logs/s05_1_gptalk_dma_rebuild_scalar_scale.log`
- Packaging script: `scripts/package_gptalk_boot.py`
- Bootgen log: `logs/test_s05_1_scalar_scale_s03_fsbl_active_bit_s03_uboot_bootgen.log`
- SD staging log: `logs/test_s05_1_scalar_scale_s03_fsbl_active_bit_s03_uboot_sd_stage.log`
- Active bitstream sha256: `f013fd85702efdb0c9b5c61550609726d3900b3986e2cf9b5138d0a84017d0b3`
- Active XSA sha256: `75be2808096bcf4c735dac76f2bbc469d34de028ae23967b62f1e70db5d1d4e8`
- GEMV OOC DCP sha256: `4880c0450a771fe7867e94289eb5e091651653988308e8d4effd7d7061dfa528`
- Timing: setup WNS `0.341 ns`, hold WHS `0.019 ns`
- BOOT test folder: `artifacts/boot_tests/test_s05_1_scalar_scale_s03_fsbl_active_bit_s03_uboot`
- BOOT.BIN sha256: `caafb1588997231a72f6365f35a6786cf8b094bfeaef8de334aedd9aff5b10c2`
- SD `/BOOT.BIN` sha256: `caafb1588997231a72f6365f35a6786cf8b094bfeaef8de334aedd9aff5b10c2`
- SD 이전 BOOT backup: `/BOOT_BEFORE_TEST_S05_1_SCALAR_SCALE_S03_FSBL_ACTIVE_BIT_S03_UBOOT_20260630_185411.BIN`, sha256 `0f909df23586d376e1a4c48ae7ce9ddb24f25207ab9872e169fba9fdd5248e1f`
- BOOT 구성: known-good S03 FSBL `610d49ef...` + active scalar-scale bitstream `f013fd85...` + S03 U-Boot `79a91b6f...`
- 보드 결과: DONE/Linux prompt PASS, `ERROR_CODE=0`, `in_count=144`, `tlast_count=143`, MM2S/S2MM IOC/idle. mode=1 block_acc PASS `[-193, 38, -50]`; mode=0 scaled FAIL, latest got `[-48, 5139, 3066]` expected `[-48, 19, -6]`. Log: `logs/s05_gemv_hw_test.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 18:39:00 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.175` ns, hold WHS `0.016` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 18:53:30 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.341` ns, hold WHS `0.019` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 19:11:42 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.209` ns, hold WHS `0.016` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 19:23:04 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.373` ns, hold WHS `0.012` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-06-30 19:46:59 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.352` ns, hold WHS `0.016` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 00:30:33 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.237` ns, hold WHS `0.007` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`

## S02 빌드 산출물 기록

- 기록 시각: 2026-07-01 00:58:30 KST
- PL clock target: `74.000 MHz`
- PL clock actual FREQ_HZ: `74000000`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- XSA: `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Latest bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- Latest XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- Timing: setup WNS `0.352` ns, hold WHS `0.016` ns
- Strategy log: `logs/vivado_impl_strategy.txt`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`
