# Active 상태판

## 현재 상태

- 현재 단계: S06.5 runtime semantic correctness/demo hardening 진행 중.
- S06 functional runtime은 PASS였지만, S07 HDMI gate는 아직 ready가 아니다.
- 다음 단계: S06.5에서 chat-template 기준 FPGA readable output, ctx-size 256, max-new-tokens 16, 성능 breakdown, interactive demo 검증을 완료한 뒤 S07 재판정.
- Active S06 baseline runtime binary on board: `/tmp/smollm2_chat`
- Active S06.5 temp runtime binary on board: `/tmp/smollm2_chat_s06_5_interactive`
- Active model on board: `/opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf`
- Active Vivado project: `hw/vivado_project/GPTalk.xpr`
- PetaLinux full rebuild 대상 아님.

## S06 판정

- `--backend fpga --require-fpga --max-new-tokens 1`: PASS
- `--backend fpga --require-fpga --max-new-tokens 8`: PASS
- all Q8_0 Linear/GEMV dispatch: FPGA backend
- tied lm_head: `token_embd.weight` through FPGA backend
- CPU GEMV fallback: 0
- AXI DMA path used: yes
- input BRAM used: yes
- AXI-Lite bulk path used: no
- hot path verbose debug/status scan: no
- no checked kernel panic/oops/bus error/OOM markers

## S06.5 현재 판정

- tokenizer full decode: PASS
- chat template application/logging: PASS
- CPU-only semantic baseline: readable
- FPGA raw `Hello, how are you?` max-new-tokens 16: readable with repair pass
- CPU GEMV fallback: 0 유지
- tied lm_head: FPGA path 유지
- runtime options: `--interactive`, `--prompt-raw`, `--prompt-chat`, `--temperature`, `--top-k`, `--top-p` 구현
- board CPU interactive smoke: PASS
- role-level GEMV breakdown: q/k/v/o/gate/up/down/lm_head 출력 구현
- remaining blockers: FPGA repair overhead too high, chat max-new-tokens 16 not completed, ctx-size 256 not completed, CPU-vs-FPGA argmax mismatch
- S07 gate: not ready

## Candidate 10 Hardware Baseline

- Candidate tag: `s05_6_2_mode1_isolated_identity_scale_74MHz`
- Bitstream: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.bit`
- Bitstream sha256: `e512452259e69914e97a134ff6d781c03dd0fbf4a7fe0ca8497f0f3befd03d23`
- XSA: `hw/vivado_project/export/GPTalk_dma_s05_6_2_mode1_isolated_identity_scale_74MHz.xsa`
- XSA sha256: `4dea0c4cbf2d58acaf920afbfadf07d234fb6cdeb487818263780ba539e551ed`
- BOOT.BIN: `artifacts/boot_tests/test_s05_6_2_mode1_isolated_identity_scale_74mhz_s03_fsbl_s03_uboot/BOOT.BIN`
- BOOT.BIN sha256: `472ae39924e2f25f5ae62dd8141d0f3d1669352e97e279b76372c17b28debb5e`

S06에서는 Vivado build, bitstream/XSA/BOOT.BIN 교체, PetaLinux rebuild를 하지 않았다.

## S05.6.3 Chunk Policy

- `in_features <= 576`: no input chunking
- `in_features == 1536`: split into `512 + 512 + 512`
- each chunk packet <= 16383 bytes
- CPU only accumulates FPGA chunk `output_i32` vectors; this is not CPU GEMV fallback

## 주요 S06 산출물

- Final verify: `docs/s06_full_runtime_fpga_backend_verify.md`
- S06.5 semantic status: `docs/s06_5_runtime_semantic_correctness.md`
- S06.5 interactive mode: `docs/s06_5_interactive_mode.md`
- S06.5 demo prompts: `docs/s06_5_demo_prompts.md`
- S06.5 performance breakdown: `docs/s06_5_runtime_perf_breakdown.md`
- Baseline: `logs/s06_0_baseline.txt`
- GGUF metadata: `logs/s06_1_gguf_metadata.txt`
- Tensor dispatch map: `reports/s06_1_tensor_dispatch_map.csv`
- Tokenizer smoke: `logs/s06_2_tokenizer_smoke.txt`
- CPU-only smoke: `logs/s06_3_cpu_only_smoke.txt`
- FPGA dispatch: `logs/s06_4_fpga_backend_dispatch.txt`
- GEMV call audit: `reports/s06_4_gemv_call_audit.csv`
- Activation quant: `logs/s06_5_activation_quant.txt`
- KV memory: `logs/s06_6_kv_cache_memory.txt`
- FPGA 1-token smoke: `logs/s06_7_fpga_token_smoke_1tok.txt`
- FPGA 8-token smoke: `logs/s06_7_fpga_token_smoke_8tok.txt`
- Compare hook smoke: `logs/s06_8_compare_backend_smoke.txt`
- S06.5 interactive board smoke: `logs/s06_5_interactive_mode_board.txt`

## 재실행 명령

S06 frozen 1-token smoke:

```bash
/tmp/smollm2_chat \
  --model /opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf \
  --backend fpga \
  --require-fpga \
  --ctx-size 128 \
  --max-new-tokens 1 \
  --prompt Hi
```

S06.5 interactive CPU smoke:

```bash
printf 'Hi\n/stats\n/quit\n' | /tmp/smollm2_chat_s06_5_interactive \
  --interactive \
  --backend cpu \
  --prompt-raw unused \
  --ctx-size 128 \
  --max-new-tokens 1 \
  --temperature 0 \
  --top-k 0 \
  --top-p 1
```

## 금지사항

- S06 기록을 위해 Candidate 10 bitstream/XSA/BOOT.BIN을 덮어쓰지 않는다.
- S06 기록을 위해 Vivado build 또는 PetaLinux full rebuild를 실행하지 않는다.
- CPU GEMV fallback을 숨겨 PASS 처리하지 않는다.
- tied lm_head를 CPU GEMV로 숨기지 않는다.
- AXI-Lite bulk data path로 복귀하지 않는다.
