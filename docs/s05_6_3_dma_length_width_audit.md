# S05.6.3 DMA Length Width Audit

## Current AXI DMA

- XCI: `hw/vivado_project/GPTalk.srcs/sources_1/bd/design_1/ip/design_1_axi_dma_0_0/design_1_axi_dma_0_0.xci`
- `C_SG_LENGTH_WIDTH`: `14`
- `c_sg_length_width`: `14`
- `C_INCLUDE_SG`: `0`
- `c_include_sg`: `0`
- MM2S enabled: `1`
- S2MM enabled: `1`
- MM2S stream width: `128` bits

## Width Options

- 14 bits: max BTT 16,383 bytes. Current setting. 576-wide row-groups fit; 1536-wide row-groups fail.
- 15 bits: max BTT 32,767 bytes. Covers current 135M 1536-wide row-groups.
- 16 bits: max BTT 65,535 bytes. Covers current 135M and documented future 2560-wide row-groups.
- 18 bits: max BTT 262,143 bytes. Not needed for current row-group jobs.

## Implementation Risk

- Simple mode can remain enabled as long as `C_INCLUDE_SG` stays 0.
- The BD/IP change is localized to AXI DMA configuration; address map and PS7 interfaces do not need deletion or regeneration.
- Expected resource/timing impact is low because data width, clocks, and interconnect topology do not change.
- A full regenerate/rebuild is still required to avoid stale AXI DMA output products.
- Plan B implementation must use separate bitstream/XSA/BOOT names and rerun the full S05.5/S05.6 regression sequence.

See `reports/s05_6_3_dma_length_width_options.csv` for the numeric table.
