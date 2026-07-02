# S06.5 Runtime Performance Breakdown

## Verdict

Performance is not S07-ready.

The repaired runtime restores readable raw FPGA output, but it does so by issuing many extra FPGA repair jobs for saturated row outputs. This is correct enough for semantic debugging, not fast enough for a demo.

## Measurements

Evidence:

- `reports/s06_5_runtime_perf_breakdown.csv`
- `reports/s06_5_long_generation_matrix.csv`
- `logs/s06_5_fpga_repaired_raw_hello_16tok.txt`
- `logs/s06_5_fpga_repaired_chat_hello_4tok.txt`
- `logs/s06_5_interactive_mode_board.txt`
- `logs/s06_5_role_breakdown_host_cpu.txt`

Raw `Hello, how are you?`, max-new-tokens 16:

- total FPGA time: `198732.169 ms`
- per-token latency: `12462.203 ms`
- total GEMV calls: `4426`
- FPGA GEMV calls: `4426`
- CPU fallbacks: `0`
- FPGA repair jobs: `169970`

Chat `Hello, how are you?`, max-new-tokens 4:

- total FPGA time: `321339.732 ms`
- per-token latency: `80697.278 ms`
- total GEMV calls: `8194`
- FPGA repair jobs: `282777`

Board CPU interactive smoke, raw `Hi`, max-new-tokens 1:

- per-token latency: `3251.358 ms`
- total GEMV calls: `211`
- CPU fallbacks: `0`
- role GEMV breakdown printed for q/k/v/o/gate/up/down/lm_head
- `/stats` and `/quit` completed

## Interpretation

The major FPGA bottleneck is not tokenizer, sampling, or CPU non-GEMV work. It is the extra FPGA row-duplicate repair jobs. Role-level GEMV counters are now printed for q/k/v/o/gate/up/down/lm_head. Fine-grained driver timings for packet preload, input BRAM write, DMA setup, GEMV wait, S2MM read, and chunk accumulation still need instrumentation before performance optimization.

## Next Optimization Targets

- Identify why mixed-row packets saturate while duplicated-row packets compute correctly.
- Replace per-row repair with a lower-overhead packet schedule if possible.
- Use the role-level q/k/v/o/gate/up/down/lm_head counters to identify which projections dominate repair jobs on the FPGA path.
- Add driver timing buckets around packet copy, input BRAM write, DMA setup, wait, and result read.
- Only after numerical correctness is stable, reduce UIO/DMA setup overhead and reuse buffers aggressively.
