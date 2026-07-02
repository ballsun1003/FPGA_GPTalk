# S06.5 ACT_SHIFT Selection

## Verdict

Keep `--act-shift 8` as the runtime default for now.

The pre-repair sweep showed that ACT_SHIFT alone did not solve semantic correctness. All candidates 4 through 9 produced saturated top-k logits and failed first-token agreement. The repaired runtime changes the dominant failure mode by recomputing exact saturated FPGA row outputs on FPGA.

## Sweep Summary

Evidence:

- `logs/s06_5_act_shift_sweep.txt`
- `reports/s06_5_act_shift_sweep.csv`

Pre-repair result:

- shift 4: top score `134217728`, input_saturations `26808`
- shift 5: top score `67108864`, input_saturations `16904`
- shift 6: top score `33554432`, input_saturations `10538`
- shift 7: top score `16777216`, input_saturations `5421`
- shift 8: top score `8388608`, input_saturations `2524`
- shift 9: top score `4194304`, input_saturations `1337`

Those values track `INT32_MAX / 2^ACT_SHIFT`, so the root problem was saturated FPGA row outputs, not only activation scaling.

## Current Policy

- Default: `--act-shift 8`
- Keep ACT_SHIFT configurable.
- Do not choose a lower saturation count alone as success.
- Judge future changes by top-k agreement, repaired/unrepaired saturation counts, generated text quality, and latency.

## Repaired Runtime Observation

With `--act-shift 8`, the repaired raw 16-token `Hello, how are you?` run produced readable text with fallback 0:

```text
I hope you're doing well.

I'm looking forward to seeing you
```

The same run still needed `169970` FPGA repair jobs, so ACT_SHIFT selection is not the remaining bottleneck.
