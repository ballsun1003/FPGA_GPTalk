# Quarantine 2026-07-01 S05.5 Cleanup

This directory contains files moved out of active paths after S05.5 128-bit AXIS board PASS.

Nothing here is used by the current active flow. Files were isolated instead of deleted to preserve forensic traceability.

## Active Items Kept Outside This Quarantine

- Current active status: `docs/00_ACTIVE_KR.md`
- Current 128-bit S05.5 doc: `docs/s05_5_128bit_axis_bringup.md`
- Current S05.5 BOOT: `artifacts/boot_tests/test_s05_5_axis128_bram_scalar_74mhz_s03_fsbl_s03_uboot`
- Preserved 32-bit known-good aliases:
  - `hw/vivado_project/export/GPTalk_dma.bit`
  - `hw/vivado_project/export/GPTalk_dma.xsa`
  - `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
  - `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- Current 128-bit S05.5 exports:
  - `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.bit`
  - `hw/vivado_project/export/GPTalk_dma_s05_5_axis128_bram_scalar_74MHz.xsa`
- Active Vivado project: `hw/vivado_project/GPTalk.xpr`

## Quarantined Categories

- `docs/`: superseded S03 and S05.1/S05.2 history docs.
- `artifacts/boot_tests/`: failed, intermediate, or superseded BOOT test folders.
- `artifacts/misc/`: temporary Vivado probe projects and rejected S05.2 artifacts.
- `hw_export/`: failed, intermediate, or superseded bitstream/XSA exports.

## Restore Rule

Do not restore anything from this directory into active paths unless a later task explicitly needs the historical artifact and the restored path is documented in `docs/00_ACTIVE_KR.md`.
