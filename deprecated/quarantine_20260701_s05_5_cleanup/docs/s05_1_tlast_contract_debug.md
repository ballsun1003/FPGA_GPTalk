# S05.1 stream/TLAST contract debug

Date: 2026-06-30 KST

## 판정

S05 board failure is reproduced by the RTL testbench as an early input TLAST at
packet word 142. The C packet contract and `gemv_q8_0_stream_core.v` contract
both expect TLAST at packet word 143, so the failure is not a C packet geometry
mismatch.

The root RTL issue found during S05.1 is that `gemv_q8_0_stream_core.v`
consumed S_AXIS input when `TVALID` was high without requiring
`TVALID && TREADY`. That violates AXI Stream handshaking and can make the core's
internal word counter advance differently from the DMA/FIFO stream, producing a
TLAST mismatch at `debug_lane=8` even when software programs a 576-byte MM2S
transfer.

The stream core now consumes input only on `s_axis_fire = s_axis_tvalid &&
s_axis_tready`. RTL simulation passes after this fix. PetaLinux rebuild is not
part of this failure.

## Board failure signature

Source: `logs/s05_gemv_hw_test.txt`

- mode=0 scaled: FAIL
- mode=1 block_acc: FAIL
- GEMV `ERROR_CODE=2`
- debug row/block/lane: `row=0 block=0 lane=8`
- AXI DMA MM2S completed/idle with IOC, S2MM did not complete
- AXI DMA MM2S/S2MM and input BRAM were used
- AXI-Lite bulk data path was not used
- no kernel panic/oops/bus error observed

`debug_lane=8` is the RTL `weight_lane_base` value at the error beat. It is not
an 8-lane configuration.

## C packet dump

Generated files:

- `logs/s05_packet_words.txt`
- `logs/s05_packet_summary.txt`

S05 fake_gemv packet summary:

```text
packet_bytes: 576
scale_bytes: 64
weight_bytes: 512
total_words: 144
scale_words: 16
weight_words: 128
first_weight_word: 16
last_word_index: 143
expected_tlast_word: 143
expected_tlast_weight_col: 31
expected_tlast_lane_base: 12
dma_mm2s_len: 576
padded_scale_nonzero_count: 0
padded_weight_nonzero_count: 0
contract_check: PASS
```

The last words are:

```text
140 0560 weight - 31 0 0x0004fe20 0
141 0564 weight - 31 4 0x00000000 0
142 0568 weight - 31 8 0x00000000 0
143 0572 weight - 31 12 0x00000000 1
```

Therefore software expects TLAST only on word 143, byte offset 572,
`weight_col=31`, `lane_base=12`.

## C-side guards

`runtime_c/gemv_hw_test.c` now has static and runtime checks for:

- `packet_bytes == 576`
- `dma_mm2s_len == packet_bytes`
- `total_words == 144`
- `scale_words == 16`
- `weight_words == 128`
- `expected_tlast_word == 143`
- `expected_tlast_weight_col == 31`
- `expected_tlast_lane_base == 12`
- padded scale lanes are zero
- padded weight lanes are zero

If the contract check fails, the program exits before starting DMA.

GCC host validation was rerun after `gcc` became available:

- compile: `gcc -std=c11 -Wall -Wextra -Werror -o /tmp/s05_gemv_hw_test_hostcheck runtime_c/gemv_hw_test.c`
- compile result: PASS
- host execution: `/tmp/s05_gemv_hw_test_hostcheck --golden-dir builtin`
- host execution result: packet contract PASS, then expected host-side stop at
  missing `/sys/class/uio`
- log: `logs/s05_1_gcc_hostcheck.txt`

## RTL stream-core contract

Files checked:

- `vivado_ip/rtl/gemv_q8_0_stream_core.v`
- `vivado_ip/rtl/gemv_q8_0_dma_top.v`

For the S05 fake_gemv case:

- scale header expected by RTL: 16 stream words
- weight payload expected by RTL: `32 columns * 16 lanes / 4 bytes = 128` stream words
- total expected stream words: 144
- TLAST expected location: final word of the whole input stream, not scale
  header end, block end, or row-group start
- final expected beat: `weight_col=31`, `weight_lane_base=12`, packet word 143

`gemv_q8_0_stream_core.v` checks input TLAST in `ST_WEIGHT_RECV` only when an
AXI stream beat is accepted:

```text
s_axis_fire = s_axis_tvalid && s_axis_tready
```

The expected final-beat condition is:

```text
final_input_word =
  last_group && last_block &&
  weight_col == 31 &&
  weight_lane_base + 4 >= 16
```

For the current one-row-group, one-block case, that is true only at
`weight_col=31`, `weight_lane_base=12`.

`debug_lane` is assigned from the current `weight_lane_base` in
`ST_WEIGHT_RECV`, so `debug_lane=8` means the mismatch was detected while the
core was accepting the word for lanes 8..11. In simulation this exact value is
produced by TLAST at word 142.

`gemv_q8_0_dma_top.v` does not alter S_AXIS TLAST:

- `S_AXIS_TDATA/TVALID/TREADY/TLAST` are wired directly into the stream core.
- `M_AXIS_TLAST` is wired directly from the core output.
- `ctrl_weight_stream_length` is passed to the AXI-Lite control block as
  metadata/status only; the stream core does not use it to count or generate
  input TLAST.

## RTL fix

Modified files:

- `vivado_ip/rtl/gemv_q8_0_stream_core.v`
- `vivado_ip/tb/tb_gemv_q8_0_stream_core.sv`

The stream core input states now consume scale and weight words only on
`s_axis_fire`. The testbench was extended to drive the exact S05 packet by word
index and to verify:

- normal packet: TLAST at word 143 passes
- early TLAST: word 142 produces `ERR_TLAST` with `debug_lane=8`
- missing TLAST: produces `ERR_TLAST`
- late/extra TLAST: produces `ERR_TLAST`

## RTL simulation

Log:

- `logs/s05_1_rtl_tlast_tb_vivado.log`
- `logs/gemv_sim_project/gemv_q8_0_sim.sim/sim_1/behav/xsim/simulate.log`
- summary: `logs/s05_1_rtl_tlast_tb.txt`

Result:

```text
[PASS] mode=0 scaled outputs matched golden/fake_gemv
[PASS] mode=1 block-acc outputs matched golden/fake_gemv
[PASS] normal packet TLAST at word 143 matched golden/fake_gemv in both modes
[PASS] early TLAST at word 142 produced ERR_TLAST debug_row=0 debug_block=0 debug_lane=8
[PASS] missing TLAST produced ERR_TLAST debug_row=0 debug_block=0 debug_lane=12
[PASS] extra beat late TLAST at word 144 produced ERR_TLAST debug_row=0 debug_block=0 debug_lane=12
[PASS] tb_gemv_q8_0_stream_core completed
```

The normal packet passes in both output modes. The early TLAST case reproduces
the board's `ERROR_CODE=2` and `debug_lane=8`.

## Current mismatch assessment

- C packet total words: 144
- C expected TLAST word: 143
- RTL expected TLAST word: 143
- Board-observed equivalent: TLAST asserted at word 142, or the core observed
  the word-142 beat with TLAST high.

The mismatch was caused by the stream core accepting input beats on `TVALID`
alone instead of the AXI Stream `TVALID && TREADY` handshake. On the board, this
can make the core's internal position differ from the DMA handshaked stream and
turn an otherwise legal final TLAST into an observed `ERR_TLAST` at
`debug_lane=8`.

## Build artifacts

Vivado rebuild after the RTL fix:

- command: `env GPTALK_PL_CLK_MHZ=74 GPTALK_PL_ACTUAL_FREQ_HZ=74000000 /tools/Xilinx/Vivado/2024.2/bin/vivado -mode batch -source scripts/build_gptalk_dma_bitstream.tcl > logs/s05_1_gptalk_dma_rebuild.log 2>&1`
- result: PASS
- timing: setup WNS `0.033 ns`, hold WHS `0.013 ns`
- bitstream: `hw/vivado_project/export/GPTalk_dma.bit`
- bitstream sha256: `60472431c6b4a7d34b79556ef75c0cc2319c93f9eca98e9b2dff7464f180bbdc`
- XSA: `hw/vivado_project/export/GPTalk_dma.xsa`
- XSA sha256: `9f0683d4258e6fed9f1a56680e38168cfafa41433d784e9ec99b0c623fff16cb`

This first rebuild was not sufficient for the board. The top bitstream was
rebuilt, but the GEMV module_ref OOC DCP remained stale:

```text
hw/vivado_project/GPTalk.runs/design_1_gemv_q8_0_dma_top_0_0_synth_1/design_1_gemv_q8_0_dma_top_0_0.dcp
timestamp: 2026-06-29 17:10:43 KST
```

The first S05.1 board rerun therefore still used the old GEMV RTL and failed
again with `ERROR_CODE=2 debug_lane=8`.

The build script has been fixed to reset/rebuild the GEMV module_ref OOC run
before top synthesis and implementation. OOC-rebuilt artifacts:

- log: `logs/s05_1_gptalk_dma_rebuild_ooc.log`
- GEMV OOC DCP sha256: `99ddcca7ca30b077df2d1bc30f1d3d4426770696c368e85e6be8a6a79b886947`
- bitstream sha256: `9a39b3b321fc93c89c2140b1c98aba14a1e4678622f6e6dc8e2aa9189e220da6`
- XSA sha256: `aafd118979c6635220fcdff84b2b643d18aa726d6678160d11f7a4d89cb84c34`
- timing: setup WNS `0.001 ns`, hold WHS `0.008 ns`

S05.1 BOOT image prepared and staged to SD:

- folder: `artifacts/boot_tests/test_s05_1_s03_fsbl_handshake_bit_s03_uboot/`
- FSBL: known-good S03 FSBL
- bitstream: S05.1 handshake-fix `GPTalk_dma.bit`
- U-Boot: S03 U-Boot
- BOOT.BIN sha256: `ce5e2939614531a14e6957b17ee9ff6fa3ec7103ca0031a45068e6bd71144b97`
- manifest: `artifacts/boot_tests/test_s05_1_s03_fsbl_handshake_bit_s03_uboot/MANIFEST.txt`
- SD target: `/dev/sdb1:/BOOT.BIN`
- SD previous BOOT backup: `/dev/sdb1:/BOOT_BEFORE_S05_1_HANDSHAKE_20260630.BIN`
- SD previous BOOT backup sha256: `03b92ed1440d22a3e8dd08e318cc5e9a1f4c2a0a477ab1d1d2e6e113bdb95030`
- SD bootfs unmounted after `sync`

That staged BOOT was the first, stale-OOC attempt and failed on board. The
correct OOC-rebuilt BOOT is prepared here and still needs SD staging:

- folder: `artifacts/boot_tests/test_s05_1_ooc_rebuilt_s03_fsbl_handshake_bit_s03_uboot/`
- BOOT.BIN sha256: `0c6d39192a51b78e2cbd8c711ca9958a21563ead372c464548eac90968a9a720`
- manifest: `artifacts/boot_tests/test_s05_1_ooc_rebuilt_s03_fsbl_handshake_bit_s03_uboot/MANIFEST.txt`

## Final board result

S05 PASS was achieved after the later valid-row emit fix.

Final active passing BOOT:

- folder: `artifacts/boot_tests/test_s05_1_valid_emit_s03_fsbl_active_bit_s03_uboot/`
- BOOT.BIN sha256: `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`
- bitstream sha256: `f774ab97d114e4a0cca5df30ca1caa71197c34d109e3d12e830cf0ff83bb233c`
- timing: setup WNS `0.352 ns`, hold WHS `0.016 ns`

Board rerun result, repeated twice in the same boot state and once more after
power-cycle:

```text
mode=0 scaled: PASS
mode=1 block_acc: PASS
AXI-Lite bulk data path used: no
AXI DMA MM2S/S2MM used: yes
input BRAM used: yes
OVERALL PASS
```

The TLAST fix was necessary but not alone sufficient. Subsequent fixes also
addressed stale OOC DCP rebuild, input BRAM read latency, and output emit
sequencing. The final passing output path emits only valid rows with
`valid_lanes_reg` instead of scanning padded lanes after the last valid row.

No PetaLinux full rebuild was needed for S05.

Power-cycle rerun log:

- `logs/s05_gemv_hw_test_power_cycle_pass_20260630_1952.txt`
- sha256 `fbc5a30c8e98036e3223d0f00b383566d9757aaf534d8bdbff128ad2494d9106`
