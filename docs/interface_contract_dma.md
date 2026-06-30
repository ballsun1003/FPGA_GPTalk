# DMA GEMV Interface Contract

This visible contract mirrors the active internal contract in
`docs/internal/interface_contract_dma.md`.

Current HDMI+DMA hardware:

- Video VDMA DDR path: PS `S_AXI_HP0`
- GEMV AXI DMA DDR path: PS `S_AXI_HP1`
- GEMV control: `0x43CA0000`
- AXI DMA: `0x40400000`
- Input BRAM: `0x42000000`
- HDMI VDMA: `0x43010000`
- HDMI VTC: `0x43C10000`
- HDMI dynclk: `0x43C20000`

Policy: GEMV must use a PS HP DDR port. Do not require HP0 specifically when
HP0 is already used by video; the current expected GEMV HP port is HP1.
