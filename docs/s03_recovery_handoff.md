# S03 recovery handoff

Date: 2026-06-30 05:40 KST

## 현재 상태

S03은 완료가 아니다.

S03 bootgen fallback으로 SD bootfs에 올렸던 `BOOT.BIN`/`image.ub`는 보드에서 rootfs mount 이후 kernel panic이 발생했다. 원인은 현재 `GPTalk_dma.xsa` PL과 맞지 않는 2017 데모 계열 device tree였다.

확인된 stale DT 계열:

- `ov5640`
- MIPI CSI2 RX subsystem
- video capture / frame buffer write
- `xilinx_drm`
- 구형 camera/video 주소 `0x43C60000`, `0x43C80000`
- 구형 HDMI/VDMA/VTC 주소 `0x43000000`, `0x43C00000`

Rootfs mount 자체는 통과했으므로, 우선 문제는 SD rootfs보다 kernel/DT와 PL mismatch 쪽으로 본다.

## 이번에 staging한 recovery image

PetaLinux tools는 PATH에 없었다. Vivado 2024.2 `xsct-trim` 기반 HSI device tree 자동 생성도 `xillib_internal.tcl` 누락으로 실패했다.

그래서 전체 PetaLinux 재빌드는 하지 못했다. 대신 기존 S03 `image.ub`의 Linux kernel은 유지하고, FIT 안의 FDT만 현재 GPTalk DMA/HDMI PL address map에 맞춘 minimal DT로 교체했다. 또한 현재 Vivado export의 `ps7_init*`를 사용해 FSBL을 새로 빌드하고, 새 `BOOT.BIN`을 생성했다.

주의: artifact 디렉터리 이름은 `artifacts/s04_linux_dt`로 남아 있지만, 이것은 새 단계 완료를 뜻하지 않는다. 현재 의미는 S03 recovery staging 산출물이다.

Staging files:

- `artifacts/s04_linux_dt/bootfs/BOOT.BIN`
- `artifacts/s04_linux_dt/bootfs/image.ub`
- `artifacts/s04_linux_dt/gptalk_dma_minimal.dts`
- `artifacts/s04_linux_dt/gptalk_dma_minimal.dtb`
- `artifacts/s04_linux_dt/zynq_fsbl_gptalk_2024.2.elf`

Hashes:

- `BOOT.BIN`: `908532f0979255923d738347f7da2e60742cbeee`
- `image.ub`: `749cd8ba365651878caeb78c82a04f29e8250a5b`
- `gptalk_dma_minimal.dtb`: `dbf026b1f4172cff6f8b7f7b4ef84cf70f0c34d4`
- `zynq_fsbl_gptalk_2024.2.elf`: `f71b337c73520075ac9d2e19fde486a23ed50f47`

## Minimal DT 내용

새 minimal PL 노드는 `generic-uio`로 노출했다.

- AXI DMA: `0x40400000`
- Input BRAM controller: `0x42000000`
- HDMI VDMA: `0x43010000`
- HDMI VTC: `0x43C10000`
- HDMI dynclk: `0x43C20000`
- GEMV control: `0x43CA0000`

Bootargs:

```text
console=ttyPS0,115200 earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait
```

Verification already done:

- FIT kernel hash unchanged from original S03 image: `dcd4792a433e937558afff4e8f0647229e7c6e44`
- FIT FDT hash: `dbf026b1f4172cff6f8b7f7b4ef84cf70f0c34d4`
- `strings gptalk_dma_minimal.dtb`에서 `ov5640`, `mipi`, `csi`, `video_cap`, `xilinx_drm`, `v_frmbuf`, `43c60000`, `43c80000`, `43000000`, `43c00000` 미검출

## BOOT.BIN 구성

새 `BOOT.BIN` 구성:

1. `zynq_fsbl_gptalk_2024.2.elf`
2. `GPTalk_dma.bit`
3. `u-boot.elf`

FSBL build notes:

- FSBL source/BSP: Xilinx `embeddedsw` `xlnx_rel_v2021.1`
- PS init source: `hw/vivado_project/export/ps7_init.c`, `.h`, `_gpl.c`, `_gpl.h`
- Toolchain: local extracted `arm-none-eabi`
- FSBL compile option: `-DMMC_SUPPORT`
- `bootgen v2024.2.2` reports `Bootimage generated successfully`

## SD 적용 상태

Detected target during copy:

- SD bootfs: `/dev/sda1`, mounted at `/run/media/pjs/bootfs`
- SD rootfs: `/dev/sda2`, label `rootfs`
- Workspace USB: `/dev/sdb`, mounted at `/run/media/pjs/6A43-DC8A`

Copied to SD bootfs:

- `/run/media/pjs/bootfs/BOOT.BIN`
- `/run/media/pjs/bootfs/image.ub`

Existing S03 files were backed up before overwrite:

- `/run/media/pjs/bootfs/BOOT_S03.BIN`
- `/run/media/pjs/bootfs/IMAGE_S03.UB`

Post-copy hashes matched staging:

- SD `BOOT.BIN`: `908532f0979255923d738347f7da2e60742cbeee`
- Staged `BOOT.BIN`: `908532f0979255923d738347f7da2e60742cbeee`
- SD `image.ub`: `749cd8ba365651878caeb78c82a04f29e8250a5b`
- Staged `image.ub`: `749cd8ba365651878caeb78c82a04f29e8250a5b`

After copy, bootfs was remounted read-only.

## 다음에 이어서 할 일

첫 번째 할 일은 보드 UART boot 검증이다.

- UART device: `/dev/ttyUSB1`
- Baud: `115200`
- 기대: rootfs mount 이후 old OV5640/MIPI/video capture probe가 없어야 한다.
- 기대: kernel panic 없이 shell 또는 init 진행까지 가야 한다.
- shell 도달 시 `/sys/class/uio` 확인

아직 남은 한계:

- 이것은 full PetaLinux kernel/rootfs rebuild가 아니다.
- Kernel은 여전히 original S03 `image.ub`의 Linux 4.9.0 Xilinx image다.
- recovery image로도 kernel panic이 남으면, 다음 실제 작업은 `hw/vivado_project/export/GPTalk_dma.xsa` 기준 PetaLinux project 재생성이다.
