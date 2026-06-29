# Active 상태판

## 현재 단계

- 현재 단계: S02 완료, S03 PetaLinux/SD/boot 구성 대기
- Active Vivado project: `hw/vivado_project/GPTalk.xpr`
- Vivado GUI에서 열 파일: `hw/vivado_project/GPTalk.xpr`

## Active build script

- BD 생성/갱신: `scripts/create_or_update_gptalk_dma_bd.tcl`
- S02 bitstream/XSA build: `scripts/build_gptalk_dma_bitstream.tcl`
- 실패 report 수집: `scripts/report_failed_impl.tcl`

## 다음에 실행할 명령

S03에서 다음 단계를 진행한다. S02 bitstream/XSA는 이미 생성되어 있다.

```bash
cat docs/internal/hw_dma_bringup_result.md
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
- `scripts/create_or_update_gptalk_dma_bd.tcl`: PASS
- GPTalk 내부 BD validate: PASS
- GPTalk top: `design_1_wrapper`
- Address map: `logs/hw_dma_address_map.txt`
- S02 synthesis/implementation/bitstream/XSA: PASS
- S02 최고 클럭 예측/적용: PASS, actual `76.929 MHz`
- Timing summary: setup WNS `0.000 ns`, setup TNS `0.000 ns`, hold WHS `0.025 ns`, hold THS `0.000 ns`
- S02 verify log: `logs/s02_bitstream_xsa_verify.txt`
- Clock prediction log: `logs/s02_clock_prediction.txt`

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
