# S05.6.3 DMA Length Strategy

## Frozen Baseline

- Candidate tag: `s05_6_2_mode1_isolated_identity_scale_74MHz`
- Candidate 10 bitstream/XSA/BOOT hashes are recorded in `logs/s05_6_3_baseline_freeze.txt`.
- Candidate 10 artifacts must not be overwritten.
- S06 full runtime remains blocked until S05.6.3 passes.

## Problem

- Current AXI DMA simple length width is `14` bits.
- Current max simple BTT is `16383` bytes.
- A 576-wide row-group packet is 10,368 bytes and fits.
- A 1536-wide row-group packet is 27,648 bytes and does not fit.
- Therefore `F_1536x16` and `down_proj` row-groups are immediate blockers.

## Plan A: Software Input-Dimension Chunking

- Keep Candidate 10 bitstream/BOOT/XSA unchanged.
- Split 1536-wide GEMV row-groups into 3 input chunks of 512.
- Each chunk packet is 9,216 bytes, below the 16,383-byte simple DMA limit.
- The FPGA computes each chunk output for the same 16 output rows.
- The CPU only sums the FPGA chunk `output_i32` vectors in an `int64` accumulator and clamps the final 16-row output to `int32`.
- This is not CPU GEMV fallback; the CPU does not multiply input and weights.
- 576-wide row-groups stay unchunked.
- Future 360M 2560-wide row-groups would split into 5 chunks of 512.

## Plan B: DMA Length Width Expansion

- Audit only while Plan A is running; do not change Vivado unless Plan A hits the transition criteria.
- Keep AXI DMA simple mode and `C_INCLUDE_SG=0`.
- Preferred width is 16 bits, giving max BTT 65,535 bytes.
- Width 16 covers current 135M 1536-wide row-groups and future 2560-wide row-groups.
- Any implementation must export bitstream/XSA/BOOT under new names and preserve Candidate 10.

## Decision Rule

- If Plan A passes board and proxy gates, S06 may proceed with documented software chunking.
- If Plan A times out or creates excessive runtime complexity, switch to Plan B implementation.
- Do not mix Plan A software chunking and Plan B hardware width changes in the same debug attempt.

## Plan A Validation Result

- Reference validation: PASS. `F_1536x16_P0` and `F_1536x16_P6` full reference match `512+512+512` chunked reference bit-exactly.
- Board validation on Candidate 10: PASS. `F_1536x16_P0` and `F_1536x16_P6` mode0 chunked row-group tests pass with 9,216-byte chunk packets.
- Board mode1 chunk check: PASS for `F_1536x16_P6` chunk 1, confirming block-acc debug still works on the chunk path.
- Proxy integration: PASS. `lane_probe_32x16`, `mlp_576x1536_no_chunk`, `down_1536x576_chunked`, and `lm_head_576x256_no_chunk` pass.
- Proxy batch comparison: PASS for tensor repeats 1, 16, and 64 on mlp/down/lm_head proxy cases.
- AXI DMA path used: yes.
- Input BRAM used: yes.
- AXI-Lite bulk path used: no.
- CPU GEMV fallback used: no.

## Selected Path

- Selected path for S06 functional entry: Plan A software input-dimension chunking.
- Plan B status: audit complete, implementation deferred.
- S06 gate: unblocked for functional runtime integration using documented chunk policy.
