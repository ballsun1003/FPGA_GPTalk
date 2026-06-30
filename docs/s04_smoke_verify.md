# S04 Smoke Verify

Date: 2026-06-30 KST

## Result

S04 UIO/register/BRAM/AXI DMA register smoke passed on the S03 boot recovery
baseline.

- Active BOOT: `artifacts/boot_tests/test_c_s03_fsbl_active_bit_s03_uboot/BOOT.BIN`
- Smoke source: `artifacts/boot_tests/s04_uio_register_dma_smoke.c`
- Smoke log: `logs/serial_s04_uio_register_dma_smoke_run_20260630_121833.log`
- Summary log: `logs/s04_board_uio_register_dma_smoke.txt`

## Checks

| Check | Result |
| --- | --- |
| UIO name lookup, no hardcoded UIO number | PASS |
| Vivado address map vs UIO addr/size | PASS |
| GEMV register read | PASS |
| Input BRAM write/readback | PASS |
| AXI DMA reset/status register check | PASS |
| Bus error/kernel oops | None observed |

## Key Values

- `gemv_ctrl` UIO: `0x43ca0000`, size `0x1000`
- `axi_dma` UIO: `0x40400000`, size `0x10000`
- `input_bram` UIO: `0x42000000`, size `0x10000`
- GEMV `VERSION`: `0x000a0001`
- GEMV `STATUS`: `0x00000000`
- GEMV `ERROR`: `0x00000000`

## DMA Buffer Decision

The current booted Linux image has no user-space DMA buffer provider:

- no `/dev/udmabuf*`
- no `/dev/dma_proxy*`
- no `/proc/device-tree/reserved-memory`
- no `/sys/class/dma_heap`

Therefore S05 must not start a fake_gemv DMA transfer yet. Do not use a `malloc`
virtual address as a DMA physical address, and do not ignore cache coherency.

Chosen direction for S05 DMA buffers: add a real provider first, preferably a
reserved-memory DDR carveout mapped through `/dev/mem` with explicit noncached
access rules, or an equivalent `udmabuf`/`dma-proxy` provider. Until that is
present and verified, DMA data transfer is blocked.

## Next Step

S05 can proceed only after the DMA buffer provider is added and verified.
Register-only and BRAM-only access are already proven.
