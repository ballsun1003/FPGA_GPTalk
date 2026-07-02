# S06.5 Interactive Runtime Mode

## Verdict

Interactive mode is implemented and smoke-tested on the board for CPU backend. It is not yet S07-ready for FPGA demo use because the repaired FPGA path still has excessive latency.

## Implemented Controls

- `--interactive`
- `--ctx-size`
- `--max-new-tokens`
- `--backend cpu|fpga`
- `--require-fpga`
- `--act-shift`
- `--prompt-raw`
- `--prompt-chat`
- `--temperature`
- `--top-k`
- `--top-p`
- `/reset`
- `/stats`
- `/quit`

Each generation prints `generated_text`, generated token ids, `tokens_generated`, `total_gemv_calls`, `fpga_gemv_calls`, `cpu_gemv_fallbacks`, `per_token_latency_ms`, `ctx_used`, `ctx_max`, `KV cache bytes`, backend, `act_shift`, `chat_template_applied`, and q/k/v/o/gate/up/down/lm_head GEMV role breakdown.

## Board Smoke

Command:

```sh
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

Observed:

- board source hash matched host source hash: `57835abdc861c61f4896acb8c5d4a19b9294562b7a91febb61c469206df57ba2`
- board binary hash: `740b29077eb37b27c71fff86df07970afb2835b51a7aba419c8923eaca53f925`
- generated token id: `28`
- generated text: `,`
- `tokens_generated=1`
- `total_gemv_calls=211`
- `fpga_gemv_calls=0`
- `cpu_gemv_fallbacks=0`
- role breakdown printed for q/k/v/o/gate/up/down/lm_head
- `/stats` worked
- `/quit` worked
- run returned `0`

Evidence: `logs/s06_5_interactive_mode_board.txt`.

## FPGA Command Shape

```sh
/tmp/smollm2_chat_s06_5_interactive \
  --interactive \
  --backend fpga \
  --require-fpga \
  --prompt-chat unused \
  --ctx-size 256 \
  --max-new-tokens 16 \
  --act-shift 8 \
  --temperature 0 \
  --top-k 0 \
  --top-p 1
```

This is an interface-ready command, not an S07-ready demo acceptance result.
