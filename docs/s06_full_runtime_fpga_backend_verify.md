# S06 Full SmolLM2 Runtime FPGA Backend Verify

## Verdict

S06 PASS.

The runtime loads SmolLM2-135M-Instruct Q8_0 GGUF on Zybo Z7-20 Linux, tokenizes a prompt from GGUF tokenizer metadata, runs token generation, and dispatches all Q8_0 Linear/GEMV calls including tied lm_head through the FPGA backend under `--require-fpga`.

## Frozen Hardware Baseline

- Candidate tag: `s05_6_2_mode1_isolated_identity_scale_74MHz`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.bit`
- Bitstream sha256: `e512452259e69914e97a134ff6d781c03dd0fbf4a7fe0ca8497f0f3befd03d23`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.xsa`
- XSA sha256: `4dea0c4cbf2d58acaf920afbfadf07d234fb6cdeb487818263780ba539e551ed`
- BOOT.BIN: `artifacts/boot_tests/test_s05_6_2_mode1_isolated_identity_scale_74mhz_s03_fsbl_s03_uboot/BOOT.BIN`
- BOOT.BIN sha256: `472ae39924e2f25f5ae62dd8141d0f3d1669352e97e279b76372c17b28debb5e`

S06 did not run Vivado, did not replace bitstream/XSA/BOOT.BIN, and did not rebuild PetaLinux.

## Model / Tokenizer

- Board model path: `/opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf`
- Host model path: `quantized_model/original_gguf/SmolLM2-135M-Instruct-Q8_0.gguf`
- GGUF version: 3
- Architecture: `llama`
- Layers: 30
- Hidden size: 576
- Heads: 9
- KV heads: 3
- Vocab size: 49152
- FFN size: 1536
- RoPE dim/base: 64 / 100000.0
- Tokenizer: GGUF GPT-2 BPE metadata, 49152 tokens, 48900 merges
- BOS/EOS policy: no implicit BOS; chat turns use `<|im_end|>` id 2
- Tied lm_head policy: `token_embd.weight` is used for lm_head GEMV; embedding lookup remains CPU

## FPGA Dispatch

- Logical GEMV targets: 211
- Layer linears: 210 = 30 layers x q/k/v/o/gate/up/down
- Tied lm_head: 1
- Audit CSV: `reports/s06_4_gemv_call_audit.csv`

1-token audit:

```text
total_gemv_calls: 211
fpga_gemv_calls: 211
cpu_gemv_fallbacks: 0
```

Role counts:

```text
q_proj: 30
k_proj: 30
v_proj: 30
o_proj: 30
gate_proj: 30
up_proj: 30
down_proj: 30
lm_head_tied_token_embd: 1
```

S05.6.3 Plan A chunking is active:

- `in_features <= 576`: no input chunking
- `in_features == 1536`: `512 + 512 + 512`
- CPU accumulates FPGA chunk `output_i32` vectors only; no CPU multiply fallback

## Runtime Smoke

1-token command:

```bash
/tmp/smollm2_chat \
  --model /opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf \
  --backend fpga \
  --require-fpga \
  --ctx-size 128 \
  --max-new-tokens 1 \
  --prompt Hi
```

1-token result:

```text
generated_token_ids: 841
generated_text:  exam
total_gemv_calls: 211
fpga_gemv_calls: 211
cpu_gemv_fallbacks: 0
AXI DMA path used: yes
input BRAM used: yes
AXI-Lite bulk path used: no
input_saturations: 2513
per_token_latency_ms: 8225.550
S07 gate: ready
```

8-token result:

```text
generated_token_ids: 266 125 267 205 70 27 111 61
total_gemv_calls: 1688
fpga_gemv_calls: 1688
cpu_gemv_fallbacks: 0
AXI DMA path used: yes
input BRAM used: yes
AXI-Lite bulk path used: no
input_saturations: 20331
per_token_latency_ms: 5907.718
S07 gate: ready
```

No kernel panic/oops/bus error/OOM markers were observed in the checked dmesg tails.

## KV Cache

- ctx-size: 128
- KV dtype: float32
- KV cache bytes: 5898240
- Allocation: PASS
- KV stays in CPU DDR/RAM; it is not moved to PL

## CPU / HW Compare Hook

`--compare-backends` is implemented and passed a 1-token board smoke.

```text
CPU first token: 28
FPGA first token: 23
CPU per-token latency: 3235.623 ms
FPGA per-token latency: 5889.775 ms
CPU vs HW comparison available: yes
```

The CPU backend uses float Q8_0 dequant GEMV. The FPGA backend uses ACT_SHIFT=8 activation quantization, so exact token equality is not required for S06.

## Output Files

- `logs/s06_0_baseline.txt`
- `logs/s06_1_gguf_metadata.txt`
- `reports/s06_1_tensor_dispatch_map.csv`
- `logs/s06_2_tokenizer_smoke.txt`
- `logs/s06_3_cpu_only_smoke.txt`
- `logs/s06_4_fpga_backend_dispatch.txt`
- `reports/s06_4_gemv_call_audit.csv`
- `logs/s06_5_activation_quant.txt`
- `logs/s06_6_kv_cache_memory.txt`
- `logs/s06_7_fpga_token_smoke_1tok.txt`
- `logs/s06_7_fpga_token_smoke_8tok.txt`
- `logs/s06_8_compare_backend_smoke.txt`

## S06 Final Report

- model path: `/opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf`
- tokenizer status: GGUF GPT-2 BPE functional; raw and chat-template tokenization logged
- ctx-size: 128
- KV cache bytes: 5898240
- CPU-only status: PASS, 1 token generated/logits completed
- FPGA status: PASS
- total_gemv_calls: 211 for 1-token smoke, 1688 for 8-token smoke
- fpga_gemv_calls: equal to total in FPGA runs
- cpu_gemv_fallbacks: 0
- first generated token/text: `841` / ` exam`
- max-new-tokens achieved: 8
- per-token latency: 8225.550 ms for 1-token run; 5907.718 ms average for 8-token run
- CPU vs HW comparison available: yes
- S07 gate: ready
