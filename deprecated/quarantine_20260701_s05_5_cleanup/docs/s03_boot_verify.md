# S03 boot verify

Date: 2026-06-29

Update 2026-06-30:

S03 is not complete. The fallback image booted far enough to mount rootfs, then failed with kernel panic because the 2017 demo device tree still described old OV5640/MIPI/video capture/DRM PL nodes that do not match the current GPTalk DMA/HDMI bitstream. See `docs/s03_recovery_handoff.md` for the staged recovery image and next UART boot check.

## 1. Boot image method

Method: bootgen fallback.

Generated:

- `artifacts/s03_bootgen/BOOT.BIN`
- `artifacts/s03_bootgen/image.ub`
- `artifacts/s03_bootgen/GPTalk_dma.bit`
- `artifacts/s03_bootgen/GPTalk_dma.xsa`

Final hardware package:

- HDMI TX PL path included.
- Control/dynclk clock: `100.000 MHz`.
- Video HP/AXIS clock: `133.333 MHz`.
- GEMV/DMA clock: `74.000 MHz`.
- Timing pass: setup WNS `0.033 ns`, hold WHS `0.013 ns`.
- GEMV control base: `0x43CA0000`.
- AXI DMA base: `0x40400000`.
- HDMI VDMA/dynclk/VTC bases: `0x43010000`, `0x43C20000`, `0x43C10000`.

## 2. SD copy result

Copied to SD bootfs mounted at `/tmp/sd_bootfs`:

- `BOOT.BIN`
- `image.ub`
- `smollm2_zybo/hw/GPTalk_dma.bit`
- `smollm2_zybo/hw/GPTalk_dma.xsa`
- `smollm2_zybo/hw/hw_dma_hdmi_address_map.txt`
- `smollm2_zybo/logs/*`

Existing payload present on bootfs:

- `smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf`
- `smollm2_zybo/fpga_layout/q8_0_lane16/*`
- `smollm2_zybo/golden/fake_gemv/*`

Not copied:

- `smollm2_chat`: not present yet, S06 scope.
- `gemv_hw_test`: not present yet, S05 scope.

## 3. Read-only or write issue

Resolved for bootfs.

Observed issue:

- `bootfs` initially mounted read-only with `errors=remount-ro`.

Action:

- Unmounted `/dev/sdb1`.
- Repaired with `fsck.vfat -a /dev/sdb1`.
- Mounted read-write at `/tmp/sd_bootfs`.
- Copied files and ran `sync /tmp/sd_bootfs`.
- Unmounted `/dev/sdb1` and `/dev/sdb2` after sync.

Remaining note:

- During copy, `rootfs` was mounted read-only with `errors=remount-ro`.
- Rootfs was not modified because bootfs contains the required S03 payload.

## 4. Serial shell access

Not performed. The user requested stopping at SD writing before manual board boot.

## 5. devmem or alternate MMIO tool

Not checked. This belongs after S03 recovery board boot.

## 6. dmesg DMA/UIO errors

Not checked. This belongs after S03 recovery board boot.
