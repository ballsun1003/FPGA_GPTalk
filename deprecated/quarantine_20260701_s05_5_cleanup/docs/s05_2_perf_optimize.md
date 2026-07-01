# S05.2 performance measurement and safe optimization

Date: 2026-07-01 KST

## Baseline freeze

Passing S05 baseline was preserved before changing instrumentation.

- baseline folder: `artifacts/s05_2_perf_baseline/`
- baseline BOOT.BIN sha256: `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`
- baseline bitstream sha256: `f774ab97d114e4a0cca5df30ca1caa71197c34d109e3d12e830cf0ff83bb233c`
- baseline S05 log sha256: `fbc5a30c8e98036e3223d0f00b383566d9757aaf534d8bdbff128ad2494d9106`
- baseline pass doc sha256: `3665778bfb5a7f87cab3c85be710cdaecea4c98a3c11629b10dcb5b2f314273c`

Correctness fixes are frozen as non-negotiable:

- input stream consumes only on `TVALID && TREADY`
- input TLAST checking remains enabled
- output emits only valid rows using `valid_lanes_reg`
- AXI-Lite bulk data path remains disabled

## Instrumentation added and rejected

Software timing was added to `runtime_c/gemv_hw_test.c`:

- input BRAM write time
- MM2S buffer fill time
- S2MM buffer clear time
- DMA reset time
- GEMV control register setup time
- DMA register setup time
- wait/polling time
- total per-mode runtime

RTL performance counters were attempted behind read-only AXI-Lite registers,
but the resulting bitstream failed board regression and was rejected.

- failed BOOT.BIN sha256: `64f8bfb3abd6395748742051b611ab72908d7252bbc4a02513402d7a9f8814d7`
- failed bitstream sha256: `2c9b20b6f005d0386f5fa3b561b3d61341365e46584931bba5df3388d035f3e3`
- failed XSA sha256: `793c3854b63d12b1628aaebab7ce3c42d29e5e69b646cc6519ba05b8b7c7ce28`
- failed board log: `logs/s05_2_perf_counter_board_fail.txt`
- failed artifacts: `artifacts/s05_2_failed_perf_counter/`

The failure signature was mode=0 only:

```text
mode=0 scaled: FAIL, got [-112, 19, -6], expected [-48, 19, -6]
mode=1 block_acc: PASS
ERROR_CODE=0
internal SCALE_DBG row_acc=[-48,19,-6]
```

This means the attempted counters did not break scale arithmetic, but changed
the already-sensitive output/row_out physical implementation enough to corrupt
emitted row 0 on board. Because S05 PASS must not be broken, the RTL counter
change was removed. The interface contract does not include these registers.

## Regression

- C compile: PASS
- C host execution: packet contract PASS, then expected stop at missing host `/sys/class/uio`
- C host log: `logs/s05_2_gcc_hostcheck.txt`
- RTL simulation with counters: PASS, but board regression: FAIL
- RTL simulation after removing counters: PASS
- logs: `logs/s05_2_perf_counter_tb_vivado.log`,
  `logs/s05_2_after_counter_revert_tb_vivado.log`

The existing mode=0/mode=1 fake_gemv regression passes in simulation, but
simulation alone was not sufficient to accept the counter bitstream.

## Rejected build result

- Vivado build log: `logs/s05_2_perf_counter_bitstream_build.log`
- timing: setup WNS `0.237 ns`, hold WHS `0.007 ns`
- bitstream sha256: `2c9b20b6f005d0386f5fa3b561b3d61341365e46584931bba5df3388d035f3e3`
- XSA sha256: `793c3854b63d12b1628aaebab7ce3c42d29e5e69b646cc6519ba05b8b7c7ce28`
- GEMV OOC DCP sha256: `d29d4d3c281153eac59afb1ff3fcca530b6899dbb4c3c3bb1ba69ff07b6488dd`

BOOT packaging:

- folder: `artifacts/boot_tests/test_s05_2_perf_counter_s03_fsbl_active_bit_s03_uboot/`
- BOOT.BIN sha256: `64f8bfb3abd6395748742051b611ab72908d7252bbc4a02513402d7a9f8814d7`
- FSBL: known-good S03 FSBL
- U-Boot: S03 U-Boot
- SD staging: PASS to `/dev/sda1:/BOOT.BIN`
- previous BOOT backup: `/BOOT_BEFORE_TEST_S05_2_PERF_COUNTER_S03_FSBL_ACTIVE_BIT_S03_UBOOT_20260701_003158.BIN`

Recovery:

- SD `/BOOT.BIN` restored on-board from the backup above.
- restored BOOT sha256: `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`
- restore log: `logs/s05_2_restore_known_good_boot_serial.txt`
- active export bitstream restored to known-good hash `f774ab97d114e4a0cca5df30ca1caa71197c34d109e3d12e830cf0ff83bb233c`
- failed XSA aliases were renamed with `DO_NOT_USE`; active known-good XSA is currently not present and must be regenerated before any future XSA/PetaLinux flow.

## Board timing result

C-only timing on the known-good S05 bitstream passed twice after reboot.

Logs:

- `logs/s05_2_c_only_timing_known_good_pass.txt`
- `logs/s05_2_c_only_timing_known_good_pass_2.txt`

Representative timings from the second run:

```text
mode=0 scaled:
  input_bram_write = 15.075 us
  mm2s_fill        = 16.242 us
  s2mm_clear       = 3.834 us
  dma_reset        = 46.821 us
  gemv_setup       = 88.623 us
  dma_setup        = 251.817 us
  wait_poll        = 124.809 us
  total            = 580.626 us

mode=1 block_acc:
  input_bram_write = 9.687 us
  mm2s_fill        = 12.597 us
  s2mm_clear       = 1.137 us
  dma_reset        = 40.389 us
  gemv_setup       = 82.566 us
  dma_setup        = 233.751 us
  wait_poll        = 333442.801 us
  total            = 333849.127 us
```

Both runs passed:

```text
mode=0 scaled: PASS
mode=1 block_acc: PASS
AXI DMA MM2S/S2MM used: yes
input BRAM used: yes
AXI-Lite bulk data path used: no
OVERALL PASS
```

## Optimization assessment

No speculative datapath optimization was accepted.

Observed bottlenecks:

- S05 fake_gemv transfer is tiny: 576 B MM2S and 12 B S2MM.
- For mode=0, DMA reset/setup + GEMV MMIO setup + polling dominate the roughly
  0.58 ms total runtime.
- The S05 single-job transfer is too small for lane count to be the primary
  end-to-end bottleneck.
- Mode=1 repeatedly shows a large wait/poll component around 333 ms. Because
  the hardware result is correct and DMA reports IOC/idle, this should be
  investigated as S05 test sequencing/polling behavior before changing datapath
  width.

Safe next optimizations after board measurement:

- keep `valid_lanes_reg` output contract unchanged
- do not add wide RTL counters to the live datapath until output emit stability
  has more margin
- reduce per-job MMIO/DMA overhead by batching multiple row groups/jobs in S06
- optimize software polling and avoid fixed 1 ms sleep granularity where safe
- revisit scale pipeline only after batching/polling overhead is under control

## Lane expansion judgment

Do not move to 32 lanes yet.

With the current 32-bit AXIS stream, simply increasing lane count does not
increase the stream word bandwidth. A useful lane expansion must be designed
together with:

- 64-bit or 128-bit AXIS payload width
- revised packet layout and TLAST contract
- input BRAM banking or wider reads
- scale path parallelization
- timing closure at the target clock
- job batching to amortize DMA/MMIO setup

For the current stage, keep 16 lanes. Increasing lanes before widening AXIS and
batching jobs is not expected to produce a meaningful S05 end-to-end speedup.
