# S06.5 Demo Prompt Baseline

## Verdict

Chat template application is required for semantic testing. Raw prompts are retained only as a control.

The CPU-only runtime now generates readable text for the English chat prompt and partially usable text for the FPGA explanation prompt. Korean prompts tokenize and run, but short max-token limits can cut byte-level Korean sequences at token boundaries, producing replacement characters when generation stops before a complete UTF-8 sequence.

## Prompt Token Counts

| # | Prompt | Raw tokens | Chat tokens |
|---|---:|---:|---:|
| 1 | `Hello, how are you?` | 6 | 36 |
| 2 | `한 문장으로 자기소개해줘.` | 32 | 62 |
| 3 | `FPGA가 무엇인지 아주 짧게 설명해줘.` | 40 | 70 |
| 4 | `오늘 프로젝트가 성공했다고 축하해줘.` | 46 | 76 |

## CPU-Only Samples

| Prompt | Mode | Max new | Result |
|---|---|---:|---|
| Hello | chat | 16 | `I'm doing great! How can I help you today?` |
| Hello | raw | 32 | `I hope you're doing well. I've been thinking about the project and I think I've made some good progress...` |
| Self-intro Korean | chat | 32 | `아래의 문장으로 자기소개` |
| FPGA short explain | chat | 32 | `FPGA is a family of microprocessors that are designed to be used in embedded systems...` |
| Celebration Korean | chat | 32 | `오늘 프로젝트가 성공했다` |

## Demo Candidate

Recommended S06.5 comparison prompt:

```text
Hello, how are you?
```

Reason: CPU chat-template baseline reaches EOS in 13 generated tokens with no UTF-8 replacements:

```text
generated_token_ids: 57 5248 2567 1109 17 1073 416 339 724 346 1834 47 2
generated_text: I'm doing great! How can I help you today?
stop_reason: eos
```

Repaired FPGA raw prompt sample at max-new-tokens 16:

```text
I hope you're doing well.

I'm looking forward to seeing you
```

Repaired FPGA chat-template sample at max-new-tokens 4:

```text
I'm sorry for
```

The repaired FPGA samples are readable, but S07 is not gate-ready because chat max-new-tokens 16, ctx-size 256, and acceptable FPGA interactive latency are not established.

## Interactive Command

CPU smoke-tested on board:

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

FPGA command shape for later S07 gate testing:

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

This command is ready as a runtime interface, but it is not yet accepted as demo-ready because the repaired FPGA path is still too slow.

Secondary prompt:

```text
FPGA가 무엇인지 아주 짧게 설명해줘.
```

Reason: CPU chat-template output is readable English even for the Korean request, but it does not reach EOS by 32 tokens.

## Evidence

- Full log: `logs/s06_5_cpu_semantic_baseline.txt`
- CSV: `reports/s06_5_cpu_semantic_baseline.csv`
- Tokenizer/chat-template log: `logs/s06_5_tokenizer_decode_check.txt`
- Interactive board smoke: `logs/s06_5_interactive_mode_board.txt`
