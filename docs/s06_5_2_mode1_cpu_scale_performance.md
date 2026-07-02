# S06.5.2 Mode1 CPU Scale Performance

Candidate 10 hardware was not modified. GEMV MACs run on FPGA mode1; CPU only applies Q8_0 scale and row accumulation.

| run | tokens | repair jobs | CPU fallbacks | mode1 calls | CPU scale ops | S2MM bytes | ms/token | status |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| raw_hello_16 | 16 | 0 | 0 | 298632 | 83828736 | 335314944 | 9575.483 | pass |
| chat_hello_16 | 16 | 0 | 0 | 655032 | 183361536 | 733446144 | 21013.831 | pass |

Duplicate-row repaired baseline raw 16-token latency was about 12462.203 ms/token with 169970 repair jobs.
S07 functional demo gate should be judged from readable text plus zero repair jobs and zero CPU GEMV fallbacks.

Detailed role timing CSV: `reports/s06_5_2_mode1_cpu_scale_performance.csv`
