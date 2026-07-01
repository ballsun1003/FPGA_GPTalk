# S05.3 control/polling/DMA completion latency forensic

## Result

- Status: PASS
- Hardware/bitstream: unchanged active known-good S05 BOOT/bitstream
- RTL changes: none
- PetaLinux rebuild: not used
- AXI-Lite bulk path: not used
- S06 gate: allowed, with verbose debug/status reads disabled on the hot path

## Key finding

The old `mode=1` ~333 ms latency was not GEMV compute latency and not AXI DMA completion latency.

The first detailed timeline showed DMA and GEMV completion were already visible on the first poll:

- `first_gemv_done_ns`: about 3.4 us from poll start
- `first_mm2s_ioc_ns`: about 3.4 us from poll start
- `first_s2mm_ioc_ns`: about 3.4 us from poll start
- `first_s2mm_idle_ns`: about 3.4 us from poll start

The 333 ms came from verbose forensic overhead:

- full GEMV debug/status register reads in the pass path could cost about 333 ms
- after removing those reads, verbose serial logging can still dominate a single run
- quiet hot-path benchmark removes that artifact

The default verbose `logs/s05_gemv_hw_test.txt` run still reports a large `total` for mode=1 because it includes serial output time, but `wait_poll` now records the actual completion observation from the timeline:

- mode=1 `wait_poll`: about 3.4 us
- mode=1 verbose `total`: about 333 ms, not hot-path compute/DMA time

## Output length contract

- `mode=0` S2MM bytes: `out_features * 4 = 3 * 4 = 12`
- `mode=1` S2MM bytes: `out_features * 4 = 3 * 4 = 12`
- actual emitted words: 3
- S2MM IOC and idle: observed
- GEMV ERROR_CODE: 0

## Benchmarks

Logs:

- `logs/s05_3_timeline_mode0.txt`
- `logs/s05_3_timeline_mode1.txt`
- `logs/s05_3_repeated_benchmark.txt`
- `logs/s05_3_repeated_benchmark_busy_reset_once_reuse.txt`
- `logs/s05_3_repeated_benchmark_busy_reset_every.txt`
- `logs/s05_3_repeated_benchmark_sleep1000_reset_once_reuse.txt`

100-run quiet hot path:

- busy poll + reset once/reuse:
  - mode=0: min 241 us, avg 244 us, max 285 us, p50 241 us, p95 267 us, fail 0
  - mode=1: min 237 us, avg 241 us, max 245 us, p50 241 us, p95 244 us, fail 0
- busy poll + reset every run:
  - mode=0: min 243 us, avg 246 us, max 283 us, p50 244 us, p95 248 us, fail 0
  - mode=1: min 240 us, avg 246 us, max 286 us, p50 244 us, p95 269 us, fail 0
- sleep1000 poll + reset once/reuse:
  - mode=0: min 1306 us, avg 1313 us, max 1347 us, p50 1313 us, p95 1319 us, fail 0
  - mode=1: min 238 us, avg 917 us, max 1339 us, p50 1308 us, p95 1319 us, fail 0

## Decision

- Polling strategy: use bounded busy polling for this small S05/S06 userspace DMA path.
- Reset strategy: reset every run is safe and only about 5 us slower than reset reuse in this fake_gemv test, so keep reset-every-run as the conservative S05 default. Reset reuse is allowed as an optimization after a larger S06 batching path exists.
- Debug strategy: do not read full debug registers or print verbose serial logs in the hot path. Full debug is now gated behind `--full-debug-status`.

## S06 entry

S06 may proceed if it keeps:

- active known-good BOOT/bitstream
- 12-byte S2MM result length for this fake_gemv shape
- AXI DMA MM2S/S2MM path
- input BRAM path
- no AXI-Lite bulk data fallback
- no verbose full-debug register scan in the hot path
