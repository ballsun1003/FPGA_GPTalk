# S05.4 Real Workload Throughput Model

## Scope

This is a PC-side throughput model for the real SmolLM2-135M Q8_0 Linear/GEMV tensors. It does not integrate S06 runtime, create a new bitstream, or overwrite the active known-good BOOT/bitstream.

Input metadata:
- Tensor map: `fpga_layout/tensor_map.json`
- S05.3 overhead log: `logs/s05_3_repeated_benchmark.txt`
- CSV output: `reports/s05_4_real_workload_throughput_model.csv`

Model constants:
- lanes: 16
- Q8_0 block size: 32
- scale in FPGA packet: 4 bytes per lane
- output element: 4 bytes
- 32-bit AXIS beat: 4 bytes
- 128-bit AXIS beat: 16 bytes
- stream clock model: 74 MHz, plus 128-bit comparison at 50 MHz
- fixed S05.3 quiet hot path overhead used in model: 246.000 us
- reset_once_reuse overhead observed: 244.0 us
- reset_every_run overhead observed: 246.0 us

## Tensor Coverage

- modeled tensors: 211
- target counts: q_proj=30, k_proj=30, v_proj=30, o_proj=30, gate_proj=30, up_proj=30, down_proj=30, lm_head=1
- total packet bytes for one full pass over modeled GEMV tensors: 151,289,856
- total MACs for one full pass over modeled GEMV tensors: 134,479,872
- special target handling:
- No separate output.weight is present. token_embd.weight is modeled as the tied lm_head because pycharm/golden/lm_head_slice/manifest.json states that the lm_head slice is generated from token_embd.weight.

The tied `lm_head` row uses actual `token_embd.weight` tensor metadata from `tensor_map.json`; it is not a fabricated weight file.

## Per-Shape Summary

| target | count | in x out | blocks/row | row_groups | packet bytes | 32b beats | 32b stream us @74 | 128b beats | 128b stream us @74 | single 32b us | batch64 128b us |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| q_proj | 30 | 576x576 | 18 | 36 | 373,248 | 93,312 | 1260.973 | 23,328 | 315.243 | 1506.973 | 319.087 |
| k_proj | 30 | 576x192 | 18 | 12 | 124,416 | 31,104 | 420.324 | 7,776 | 105.081 | 666.324 | 108.925 |
| v_proj | 30 | 576x192 | 18 | 12 | 124,416 | 31,104 | 420.324 | 7,776 | 105.081 | 666.324 | 108.925 |
| o_proj | 30 | 576x576 | 18 | 36 | 373,248 | 93,312 | 1260.973 | 23,328 | 315.243 | 1506.973 | 319.087 |
| gate_proj | 30 | 576x1536 | 18 | 96 | 995,328 | 248,832 | 3362.595 | 62,208 | 840.649 | 3608.595 | 844.492 |
| up_proj | 30 | 576x1536 | 18 | 96 | 995,328 | 248,832 | 3362.595 | 62,208 | 840.649 | 3608.595 | 844.492 |
| down_proj | 30 | 1536x576 | 48 | 36 | 995,328 | 248,832 | 3362.595 | 62,208 | 840.649 | 3608.595 | 844.492 |
| lm_head | 1 | 576x49152 | 18 | 3072 | 31,850,496 | 7,962,624 | 107603.027 | 1,990,656 | 26900.757 | 107849.027 | 26904.601 |

## Largest Tensors

| tensor | target | in x out | packet bytes | MACs | 32b stream us @74 | 128b stream us @74 |
| --- | --- | --- | --- | --- | --- | --- |
| token_embd.weight | lm_head | 576x49152 | 31,850,496 | 28,311,552 | 107603.027 | 26900.757 |
| blk.0.ffn_gate.weight | gate_proj | 576x1536 | 995,328 | 884,736 | 3362.595 | 840.649 |
| blk.0.ffn_up.weight | up_proj | 576x1536 | 995,328 | 884,736 | 3362.595 | 840.649 |
| blk.0.ffn_down.weight | down_proj | 1536x576 | 995,328 | 884,736 | 3362.595 | 840.649 |
| blk.1.ffn_gate.weight | gate_proj | 576x1536 | 995,328 | 884,736 | 3362.595 | 840.649 |
| blk.1.ffn_up.weight | up_proj | 576x1536 | 995,328 | 884,736 | 3362.595 | 840.649 |
| blk.1.ffn_down.weight | down_proj | 1536x576 | 995,328 | 884,736 | 3362.595 | 840.649 |
| blk.2.ffn_gate.weight | gate_proj | 576x1536 | 995,328 | 884,736 | 3362.595 | 840.649 |

## Aggregate Estimates

- 32-bit AXIS pure stream time at 74 MHz: 511114.378 us
- 128-bit AXIS pure stream time at 74 MHz: 127778.595 us
- 128-bit AXIS theoretical stream speedup: 4.000x
- single-GEMV fixed overhead if paid per tensor: 51906.000 us
- batch64 amortized fixed overhead over modeled tensors: 811.031 us

The fixed overhead dominates the small and medium tensors if every GEMV is launched as an independent job. The tied `lm_head` is bandwidth dominated even with 128-bit AXIS. Batching is therefore a throughput requirement, not a cosmetic optimization.

## Conclusions

- 32-bit AXIS is enough for S06 correctness bring-up, but it is not enough as the final performance path. It feeds the lane16 packet as four-byte beats, so the stream beat count is 4x the 128-bit model.
- 128-bit AXIS is needed for the performance gate before judging the FPGA backend against CPU runtime. The model gives an exact 4.000x stream beat reduction for the current packet layout.
- Batching is required. S05.3 quiet hot path overhead is about 246 us per independent GEMV. It is large versus small projection stream time and still material for per-tensor launch overhead.
- The primary bottleneck is tied `lm_head` from `token_embd.weight`, with 31,850,496 packet bytes and 28,311,552 MACs. The largest transformer-block projection classes remain `gate_proj`, `up_proj`, and `down_proj`, each with 995,328 packet bytes and 884,736 MACs per tensor.
- fake_gemv timing is not final performance. The fake packet is 576 bytes, while the largest real GEMV packet is 31,850,496 bytes, about 55296.0x larger. S05 fake_gemv proves protocol/correctness, not end-to-end model throughput.

## S06 Gate

S06 may use the current 32-bit AXIS path only as a functional integration path. Before treating S06 performance as meaningful, the project needs a batching plan and a 128-bit AXIS data path model or implementation plan. S05.5 and S06 were not started by this S05.4 task.
