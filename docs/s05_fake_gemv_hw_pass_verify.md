# S05 fake_gemv HW PASS verify

Date: 2026-06-30 KST

## 판정

S05 PASS 완료.

`valid_lanes_reg` emit fix BOOT에서 보드 S05를 같은 부팅 상태로 2회
연속 재실행했고, 전원 재투입 후 1회 추가 재실행했다. 세 번 모두
`mode=0 scaled`와 `mode=1 block_acc`가 PASS했다.

## Preserved 32-bit BOOT/bitstream

- BOOT folder: `artifacts/boot_tests/test_s05_3_reexport_xsa_s03_fsbl_active_bit_s03_uboot`
- Validated backup: `artifacts/s05_3_validated_known_good_reexport_20260701_010926`
- BOOT sha256: `17a771c5cc304143a07f4444b7baf87a44fb2609f66b9ed42b4cde3757836a42`
- FSBL: known-good S03 FSBL, sha256 `610d49efca34d84d073d01566188bebde1c4a0a827d248e5b9cfbe17786d3eca`
- U-Boot: S03 U-Boot, sha256 `79a91b6f2a10e13af2c1b27b3a9bd54e513663599180e397df435bd059cd196d`
- bitstream alias: `hw/vivado_project/export/GPTalk_dma.bit`
- bitstream sha256: `158ba9de633fc8ea4a8b4822d0589fad427ec25502ff67cc8d44ef604696acb0`
- XSA alias: `hw/vivado_project/export/GPTalk_dma.xsa`
- XSA sha256: `f6ef5281f7558d21435127cc6858e72d312089e1f7a26bed34ce4206ae9a1d7d`
- GEMV OOC DCP sha256: `6826ab2bbeb37ef5548abd64e3214e382683e82b3838f0b96189ef1f361ae5e6`
- Timing: setup WNS `0.352 ns`, hold WHS `0.016 ns`

The earlier valid-emit boot-test folder was quarantined after S05.5 cleanup.
The hashes above are the preserved S05.3 reexport known-good
baseline that was cold-boot validated from a PC-staged SD card.

## Test implementation

- Source: `runtime_c/gemv_hw_test.c`
- UART runner: `scripts/s05_gemv_hw_test.py`
- Board binary: `/tmp/s05_gemv_hw_test`
- Logs: `logs/s05_gemv_hw_test_valid_emit_pass_1.txt`,
  `logs/s05_gemv_hw_test_valid_emit_pass_2.txt`,
  `logs/s05_gemv_hw_test_power_cycle_pass_20260630_1952.txt`,
  `logs/s05_gemv_hw_test.txt`
- Data path: AXI DMA MM2S/S2MM
- Input path: input BRAM UIO
- Result path: S2MM DDR buffer in `/dev/mem` carveout
- AXI-Lite bulk path: not used

## PASS result

The fake_gemv packet contract remained:

```text
total_words=144
scale_words=16
weight_words=128
expected_tlast_word=143
expected_tlast_weight_col=31
expected_tlast_lane_base=12
```

All reruns reported:

```text
mode=0 scaled: PASS
mode=1 block_acc: PASS
AXI-Lite bulk data path used: no
AXI DMA MM2S/S2MM used: yes
input BRAM used: yes
OVERALL PASS
```

Mode outputs:

```text
mode=0 scaled:    [-48, 19, -6]
mode=1 block_acc: [-193, 38, -50]
```

DMA/core status:

```text
ERROR_CODE=0
in_count=144
tlast_count=143
tlast_tkeep=0x0000000f
MM2S IOC+idle
S2MM IOC+idle
```

No kernel panic, oops, bus error, timeout, or fallback path was observed.

Power-cycle rerun:

```text
log: logs/s05_gemv_hw_test_power_cycle_pass_20260630_1952.txt
log sha256: fbc5a30c8e98036e3223d0f00b383566d9757aaf534d8bdbff128ad2494d9106
result: OVERALL PASS
```

## Root-cause summary

The original S05 failure was not a software DMA buffer provider issue.

The debug path established this sequence:

- Input packet/TLAST contract was correct.
- The core had consumed stream beats on `TVALID` without requiring
  `TVALID && TREADY`; this was fixed.
- Stale GEMV OOC DCP initially hid RTL changes; the build script now rebuilds
  the GEMV module_ref OOC run.
- Input BRAM read latency needed an extra wait stage.
- Mode=0 scale arithmetic was correct on board:
  `scale_q`, `block_acc`, `product`, `scaled`, and `row_acc` matched expected
  values.
- Output emit sequencing over padded lanes was corrupting S2MM/debug output.
  The passing fix emits only valid rows using `valid_lanes_reg`.

## Follow-up note

During debug, the mode=0 scale path was simplified to a scalar lane pipeline.
That helped isolate the issue but may not be the final performance shape. It is
safe for S05 correctness; performance recovery can be handled after the current
bring-up milestone.
