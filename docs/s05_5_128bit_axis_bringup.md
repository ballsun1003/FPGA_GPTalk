# S05.5 128-bit AXIS Bring-up

## Current Status

- Status: PASS.
- Scope: S05.5 only. Do not proceed to S05.6 or S06 without explicit user request.
- Active 32-bit known-good BOOT/bitstream/XSA aliases remain preserved.
- Current 128-bit candidate BOOT was staged on SD bootfs `/BOOT.BIN` and verified on board.

## Current Candidate

- BOOT test: `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot`
- BOOT.BIN hash: `4d7f875198fed7806b6265db126909067587ee42ec3e5db32308fd62dbd59a8c`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.bit`
- Bitstream hash: `1f873bc39b48d56275f9f07e7fd5db1b979adc118beff768c0b24a9204a7d4ac`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.xsa`
- XSA hash: `005b3f9b04cc521fca0c4979d77a8c19cdab9c0335a477cb973e48a7ec627f08`
- GEMV OOC DCP: `hw/vivado_project/GPTalk.runs/design_1_gemv_q8_0_dma_top_0_2_synth_1/design_1_gemv_q8_0_dma_top_0_2.dcp`
- GEMV OOC DCP hash: `a0086eaa87aeabd76b053f034b46458cad79ba252c6a7a6bc856d6e0a5652263`
- FSBL: known-good S03 FSBL, hash `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: S03 U-Boot, hash `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`

## Timing

- Target clock: `74 MHz`
- Setup WNS: `0.389 ns`
- Setup TNS approx: `0.000 ns`
- Hold WHS: `0.018 ns`
- Hold THS approx: `0.000 ns`
- Build log: `logs/s05_5_axis128_bram_scalar_bitstream_build.log`
- Timing report: `reports/gptalk_dma_74mhz_timing_summary.rpt`

Clock values listed in the S05.5 prompt are example sweep candidates, not mandatory fixed steps. The selected clock should be the highest clock that passes timing and board regression. Timing PASS plus board FAIL is treated as a functional bug, not as proof that the clock must be lowered.

## Root Cause Fixed

The previous 128-bit board run failed with:

- mode=0 block debug: `[2048, 256, -192]`
- mode=1 result: `[2048, 256, -192]`
- expected block_acc: `[-193, 38, -50]`
- TLAST/TKEEP/DMA/status were normal.

That observed block value exactly matches fake_gemv if the input samples seen by the core are:

- even columns: `8`
- odd columns: `0`

The GEMV module_ref XCI had `INPUT_BRAM_DOUT` default driver value `8`, and the BD left `input_vector_bram/doutb` as a one-pin net. This points to the GEMV input BRAM Port B DOUT not being actually connected to the core in the failed 128-bit BD.

Fix applied:

- `scripts/s05_5_apply_axis128_bd.tcl` now removes the BRAM interface net and connects all BRAM Port B pins as scalar nets.
- `logs/s05_5_bd_width_audit.txt` confirms:
  - `gemv_q8_0_dma_top_0/INPUT_BRAM_DOUT -> /input_vector_bram_doutb`
  - `input_vector_bram/doutb -> /input_vector_bram_doutb`
  - `GEMV INPUT_BRAM_PORT interface -> NONE`
  - `input_vector_bram BRAM_PORTB interface -> NONE`

## Board Result

- Full debug log: `logs/s05_5_axis128_bram_scalar_board_full_debug.txt`
- Quiet 100-run log: `logs/s05_5_axis128_bram_scalar_board_quiet_100.txt`
- `GEMV BUILD_CONFIG=0x00800010 axis_width=128 lanes=16`
- mode=0 scaled: PASS
- mode=1 block_acc: PASS
- AXI DMA MM2S/S2MM used: yes
- input BRAM used: yes
- AXI-Lite bulk path used: no
- TLAST/TKEEP/DMA error: none
- dmesg panic/oops/bus error: none

The failing block accumulator from the previous 128-bit build is fixed:

- previous failed block: `[2048, 256, -192]`
- current mode=0 block debug: `[-193, 38, -50]`
- current mode=0 scaled result: `[-48, 19, -6]`
- current mode=1 block_acc result: `[-193, 38, -50]`

## Quiet 100-run Regression

- poll strategy: busy
- reset strategy: reset every run
- mode=0 fail count: `0`
- mode=1 fail count: `0`
- mode=0 latency: min `249 us`, avg `251 us`, max `295 us`, p50 `250 us`, p95 `252 us`
- mode=1 latency: min `249 us`, avg `251 us`, max `281 us`, p50 `250 us`, p95 `253 us`

## 32-bit Comparison

- 32-bit S05.3 reset-every-run busy baseline:
  - mode=0 avg `246 us`
  - mode=1 avg `246 us`
- 128-bit S05.5 reset-every-run busy result:
  - mode=0 avg `251 us`
  - mode=1 avg `251 us`
- Stream beat count:
  - 32-bit: `144` beats
  - 128-bit: `36` beats
  - theoretical stream beat reduction: `4x`
- Measured fake_gemv single-job latency speedup: none; small job latency is dominated by fixed DMA/MMIO/control overhead.

S05.5 proves that 128-bit MM2S is functionally usable on board. The throughput benefit still needs S05.6-style batching/persistent-job overhead reduction before S06 runtime integration.

## Next Stage Guard

Do not continue to S05.6 or S06 until the user explicitly requests the next stage.
