# S03 SD boot packaging result

Date: 2026-06-29

Update 2026-06-30:

This document records the original S03 packaging/copy result only. S03 did not complete at board boot: the fallback image reached rootfs mount and then hit kernel panic from old demo DT/PL mismatch. A recovery `BOOT.BIN`/`image.ub` has since been staged and copied to SD bootfs; see `docs/s03_recovery_handoff.md`.

## S01/S02 precheck

- Global instruction checked: active Vivado project is `hw/vivado_project/GPTalk.xpr`.
- S01 artifact checked: `logs/s01_dma_arch_verify.txt` reports AXI DMA + AXI-Stream GEMV, no AXI-Lite bulk data path, and no custom AXI master.
- Existing S02 GEMV DMA artifact was not enough for the user's HDMI PL requirement, so HDMI TX PL was added before final S03 packaging.

## Hardware result

HDMI + GEMV DMA design:

- HDMI TX PL path: AXI VDMA MM2S -> AXIS video -> VTC -> rgb2dvi -> `hdmi_out`.
- HDMI DDC: PS I2C EMIO -> `hdmi_out_ddc`.
- GEMV datapath: AXI DMA + AXI-Stream GEMV retained.
- UART terminal: `image.ub` bootargs include `console=ttyPS0,115200`.
- USB keyboard path: `image.ub` includes Zynq USB controller strings.

Clock search:

- `76.923 MHz` attempt failed timing: setup WNS `-0.353 ns`.
- `50.000 MHz` attempt passed but was too conservative.
- Final selected GEMV/DMA clock: `74.000 MHz`.

Final clocks:

- FCLK0/control+dynclk: `100.000 MHz`
- FCLK1/video HP+AXIS: `133.333 MHz`
- FCLK2/GEMV+DMA: `74.000 MHz`

Final timing:

- setup WNS: `0.033 ns`
- setup TNS approx: `0.000 ns`
- hold WHS: `0.013 ns`
- hold THS approx: `0.000 ns`
- timing pass: `1`

Final hardware exports:

- `hw/vivado_project/export/GPTalk_dma_74MHz.bit`
- `hw/vivado_project/export/GPTalk_dma_74MHz.xsa`
- latest aliases: `hw/vivado_project/export/GPTalk_dma.bit`, `hw/vivado_project/export/GPTalk_dma.xsa`

Address map:

- AXI VDMA display base: `0x43010000`
- VTC output base: `0x43C10000`
- AXI dynclk base: `0x43C20000`
- GEMV control base: `0x43CA0000`
- AXI DMA base: `0x40400000`
- Input BRAM base: `0x42000000`

## Packaging method

S03 used the allowed bootgen fallback:

- FSBL: `artifacts/s03_bootgen/zynq_fsbl.elf`
- Bitstream: `artifacts/s03_bootgen/GPTalk_dma.bit`
- U-Boot: `artifacts/s03_bootgen/u-boot.elf`
- Linux image: `artifacts/s03_bootgen/image.ub`
- BIF: `artifacts/s03_bootgen/gptalk_dma_boot.bif`
- Generated boot image: `artifacts/s03_bootgen/BOOT.BIN`
- Bootgen log: `logs/s03_petalinux_or_bootgen_log.txt`

Bootgen reports `Bootimage generated successfully`.

## SD copy result

SD card during copy:

- `/dev/sdb1` label `bootfs`, mounted manually at `/tmp/sd_bootfs`
- `/dev/sdb2` label `rootfs`, mounted at `/run/media/pjs/rootfs`

The boot FAT filesystem initially mounted read-only with `errors=remount-ro`. It was repaired with `fsck.vfat -a /dev/sdb1`, then mounted read-write at `/tmp/sd_bootfs`.

Copied to SD bootfs:

- `/tmp/sd_bootfs/BOOT.BIN`
- `/tmp/sd_bootfs/image.ub`
- `/tmp/sd_bootfs/smollm2_zybo/hw/GPTalk_dma.bit`
- `/tmp/sd_bootfs/smollm2_zybo/hw/GPTalk_dma.xsa`
- `/tmp/sd_bootfs/smollm2_zybo/hw/hw_dma_hdmi_address_map.txt`
- `/tmp/sd_bootfs/smollm2_zybo/logs/*`

The existing bootfs model/layout/golden payload remains present:

- `/tmp/sd_bootfs/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf`
- `/tmp/sd_bootfs/smollm2_zybo/fpga_layout/q8_0_lane16/*`
- `/tmp/sd_bootfs/smollm2_zybo/golden/fake_gemv/*`

Hash verification:

- staged `BOOT.BIN` matches SD `BOOT.BIN`: `846ef51ea488bbe85be1753e6e4023eb3916dab22c03dd83339ba3a0814950e8`
- staged `GPTalk_dma.bit` matches SD `GPTalk_dma.bit`: `0053cee7664da6932bd7b70aef091de1b0569c055b200a188f6bbc2a6b59d681`
- staged `GPTalk_dma.xsa` matches SD `GPTalk_dma.xsa`: `88973c037eaaff23a1ceb6e531855e21d7080e1a547406fc94617ef0f55f94b1`

`sync /tmp/sd_bootfs` completed.

Post-copy unmount completed:

- `/dev/sdb1`
- `/dev/sdb2`

Not copied because not present in the repo yet:

- `smollm2_chat`: S06 scope.
- `gemv_hw_test`: S05 scope.

Rootfs note:

- During copy, rootfs was mounted read-only with `errors=remount-ro`.
- Bootfs contains the S03 payload, so S03 SD boot packaging is complete without rootfs writes.
