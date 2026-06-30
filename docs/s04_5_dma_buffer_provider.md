# S04.5 DMA buffer provider

## 상태

- S04.5A board-only temporary `mem=` boot: PASS
- S04.5C `/dev/mem` carveout smoke: PASS
- S04.5B permanent SD bootargs update: PASS
- PetaLinux full rebuild: not required for S04.5A/C and still not started

## Baseline

- Boot baseline: `artifacts/boot_tests/test_c_s03_fsbl_active_bit_s03_uboot/BOOT.BIN`
- S03 boot recovery: PASS
- S04 UIO/register/BRAM/DMA-register smoke: PASS
- SD files modified during S04.5A: no
- U-Boot persistent environment modified: no, `saveenv` was not issued
- Serial log: `logs/s04_5a_temp_mem_boot.txt`
- S04.5C serial/test log: `logs/s04_5c_dma_carveout_smoke.txt`
- S04.5B staging log: `logs/s04_5b_sd_bootargs_update.txt`
- S04.5B board verification log: `logs/s04_5b_boot_verify.txt`
- S04.5C source: `artifacts/boot_tests/s04_5_dma_carveout_smoke.c`
- S04.5C runner: `scripts/s04_5c_carveout_smoke.py`

## U-Boot observations

- U-Boot: `U-Boot 2017.01 (Jun 25 2026 - 08:28:41 +0000)`
- Board model: `Zynq Zybo Z7 Development Board`
- DRAM: `ECC disabled 1 GiB`
- `printenv bootargs`: undefined before the temporary test
- `printenv bootcmd`: `bootcmd=run default_bootcmd`
- `bdinfo` DDR bank:
  - start: `0x00000000`
  - size: `0x40000000`

Temporary bootargs used for this one boot:

```text
console=ttyPS0,115200 earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait mem=960M
```

Because baseline `bootargs` was undefined in the U-Boot environment, the temporary test used the known working S03 Linux command line plus only `mem=960M`.

## Linux observations

`/proc/cmdline`:

```text
console=ttyPS0,115200 earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait mem=960M
```

`/proc/meminfo` highlights:

```text
MemTotal:         965160 kB
CmaTotal:         131072 kB
CmaFree:          130788 kB
```

`/proc/iomem` RAM range:

```text
00000000-3bffffff : System RAM
```

UIO nodes after `mem=960M` boot:

```text
uio0 axi_dma     0x40400000 0x00010000
uio1 input_bram  0x42000000 0x00010000
uio2 hdmi_vdma   0x43010000 0x00010000
uio3 hdmi_vtc    0x43c10000 0x00010000
uio4 hdmi_dynclk 0x43c20000 0x00010000
uio5 gemv_ctrl   0x43ca0000 0x00001000
```

Relevant dmesg lines:

```text
cma: Reserved 128 MiB at 0x34000000
Kernel command line: console=ttyPS0,115200 earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait mem=960M
Memory: 833064K/983040K available (... 131072K cma-reserved, 65536K highmem)
DMA: preallocated 256 KiB pool for atomic coherent allocations
```

No kernel panic, oops, or rootfs mount failure was observed in the S04.5A serial log.

## Carveout candidate

- DDR physical range: `0x00000000-0x3fffffff`
- Linux System RAM with `mem=960M`: `0x00000000-0x3bffffff`
- Candidate carveout: `0x3c000000-0x3fffffff`
- Candidate size: `64 MiB`
- S04.5A verdict: candidate is outside Linux System RAM

The CMA reservation at `0x34000000` is inside Linux-managed memory and is not the selected user-space DMA carveout. The next test must use the top 64 MiB candidate above.

## S04.5C `/dev/mem` smoke

Command executed on the board:

```text
/tmp/s04_5_dma_carveout_smoke --phys-base 0x3c000000 --size 0x04000000 --pattern-count 64
```

Build result:

```text
__S04_5C_COMPILE_RC__=0
```

Run result:

```text
open /dev/mem O_RDWR|O_SYNC PASS
mmap PASS
PASS[000]: phys=0x3c000000 offset=0x00000000 pattern=0x99a50000
PASS[063]: phys=0x3ffffffc offset=0x03fffffc pattern=0x75fc0b44
SUMMARY PASS patterns=64 phys_base=0x3c000000 size=0x04000000
__S04_5C_RUN_RC__=0
```

Dmesg check after the smoke did not show panic, oops, external abort, bus error, SIGBUS, or mismatch. The only matching dmesg lines were normal DMA driver lines.

S04.5C therefore validates the MVP physical buffer layout:

```text
base: 0x3c000000
size: 0x04000000
end:  0x3fffffff
mode: /dev/mem O_RDWR | O_SYNC mmap
```

This proves CPU userspace can map and touch the selected carveout. DMA coherency is still a runtime property to verify in S05 with the AXI DMA transfer itself.

## S04.5B permanent bootargs

SD bootfs update:

```text
/uEnv.txt
bootargs=console=ttyPS0,115200 earlyprintk clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait mem=960M
```

Backup directory on bootfs:

```text
/backup/bootargs_20260630_144412/
```

Unchanged artifacts:

```text
BOOT.BIN 03b92ed1440d22a3e8dd08e318cc5e9a1f4c2a0a477ab1d1d2e6e113bdb95030
image.ub e25d925cf286c7517c4da3d7804637de37f4dc1c7c5b95d087c5c24fd7934715
```

Board boot verification after SD update:

```text
/proc/cmdline contains mem=960M
/proc/iomem: 00000000-3bffffff : System RAM
bootfs uEnv.txt: /run/media/mmcblk0p1/uEnv.txt
uEnv.txt sha256: 369ff646c0c07a3ac5343d5dd7f26f5b18ced4ca56a3e7db3d532bdf846f7c76
UIO nodes: axi_dma, input_bram, hdmi_vdma, hdmi_vtc, hdmi_dynclk, gemv_ctrl
kernel panic/oops: none observed
```

Power-cycle reverify at `2026-06-30 14:50:53 KST` also PASS:

```text
/proc/cmdline contains mem=960M
/proc/iomem: 00000000-3bffffff : System RAM
bootfs uEnv.txt sha256: 369ff646c0c07a3ac5343d5dd7f26f5b18ced4ca56a3e7db3d532bdf846f7c76
UIO nodes: axi_dma, input_bram, hdmi_vdma, hdmi_vtc, hdmi_dynclk, gemv_ctrl
kernel panic/oops: none observed
```

## 판정

S04.5A is PASS. The board can boot to the serial Linux root prompt with a temporary `mem=960M` limit, the command line reflects the limit, `/proc/iomem` excludes `0x3c000000-0x3fffffff` from System RAM, and UIO nodes remain present.

S04.5C is PASS. `/dev/mem` `O_SYNC` mmap write/readback works against `0x3c000000-0x3fffffff`.

S04.5B is PASS. The SD now applies `mem=960M` automatically through bootfs `uEnv.txt` without changing `BOOT.BIN`, `image.ub`, or rootfs. This remains true after a board power cycle.

S04.5 DMA buffer provider is PASS for the MVP `/dev/mem` carveout path. S05 may use this physical layout.
