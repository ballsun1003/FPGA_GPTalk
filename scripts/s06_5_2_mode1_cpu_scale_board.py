#!/usr/bin/env python3
import csv
import os
import re
import shlex
import sys
import time
from pathlib import Path

import serial


PORT = os.environ.get("S06_SERIAL", os.environ.get("S05_SERIAL", "/dev/ttyUSB1"))
BAUD = int(os.environ.get("S06_BAUD", os.environ.get("S05_BAUD", "115200")))
SOURCE_PATH = Path("runtime_c/smollm2_chat.c")
MODEL_PATH = "/opt/smollm2_zybo/model/SmolLM2-135M-Instruct-Q8_0.gguf"
BOARD_SOURCE = "/tmp/smollm2_chat_s06_5_2.c"
BOARD_BIN = "/tmp/smollm2_chat_s06_5_2"
PROMPT_RE = re.compile(rb"root@Zybo-Z7-20:~#")

LOG_DIR = Path("logs")
REPORT_DIR = Path("reports")
DOC_DIR = Path("docs")
SERIAL_LOG = LOG_DIR / "s06_5_2_mode1_cpu_scale_serial.txt"
LAYER_LOG = LOG_DIR / "s06_5_2_mode1_cpu_scale_layer_diff.txt"
RAW_LOG = LOG_DIR / "s06_5_2_mode1_cpu_scale_raw_hello_16tok.txt"
CHAT_LOG = LOG_DIR / "s06_5_2_mode1_cpu_scale_chat_hello_16tok.txt"
CHAT32_LOG = LOG_DIR / "s06_5_2_mode1_cpu_scale_chat_hello_32tok.txt"
QPROJ_CSV = REPORT_DIR / "s06_5_2_mode1_cpu_scale_qproj_compare.csv"
LAYER_CSV = REPORT_DIR / "s06_5_2_mode1_cpu_scale_layer_diff.csv"
LOGITS_CSV = REPORT_DIR / "s06_5_2_mode1_cpu_scale_logits_compare.csv"
GEN_CSV = REPORT_DIR / "s06_5_2_mode1_cpu_scale_generation_matrix.csv"
PERF_CSV = REPORT_DIR / "s06_5_2_mode1_cpu_scale_performance.csv"
PERF_MD = DOC_DIR / "s06_5_2_mode1_cpu_scale_performance.md"

RUN_CHAT32 = os.environ.get("S06_5_2_RUN_CHAT32", "0") == "1"
RUN_TIMEOUT = int(os.environ.get("S06_5_2_RUN_TIMEOUT", "1200"))


class Session:
    def __init__(self):
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        REPORT_DIR.mkdir(parents=True, exist_ok=True)
        DOC_DIR.mkdir(parents=True, exist_ok=True)
        self.log = SERIAL_LOG.open("wb")
        self.ser = serial.Serial(PORT, BAUD, timeout=0.05)
        self.buf = bytearray()
        self.cmd_seq = 0

    def close(self):
        self.log.flush()
        self.log.close()
        self.ser.close()

    def write(self, text):
        data = text.encode("ascii") if isinstance(text, str) else text
        self.ser.write(data)
        self.ser.flush()

    def read_until(self, predicate, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            data = self.ser.read(4096)
            if data:
                self.log.write(data)
                self.log.flush()
                self.buf.extend(data)
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                if predicate(bytes(self.buf)):
                    return True
            else:
                time.sleep(0.02)
        return False

    def read_until_prompt(self, timeout=30):
        return self.read_until(lambda b: PROMPT_RE.search(b[-65536:]) is not None, timeout)

    def cmd(self, command, timeout=30):
        self.log.write(f"\n\n### HOST_SEND: {command[:240]}\n".encode("ascii", "replace"))
        self.log.flush()
        self.buf.clear()
        self.cmd_seq += 1
        done = f"__S06_5_2_CMD_DONE_{self.cmd_seq}__"
        self.write(command + f"\necho {done}=$?\r")
        if not self.read_until(lambda b: done.encode("ascii") in b[-65536:], timeout):
            print(f"\nTIMEOUT after command: {command[:160]}", file=sys.stderr)
            return False
        self.read_until_prompt(timeout=3)
        return True

    def capture_file(self, remote_path, local_path, timeout=60):
        self.cmd_seq += 1
        start = f"__S06_5_2_CAT_{self.cmd_seq}_START__"
        end = f"__S06_5_2_CAT_{self.cmd_seq}_END__"
        self.log.write(f"\n\n### HOST_CAPTURE: {remote_path} -> {local_path}\n".encode("ascii"))
        self.log.flush()
        self.buf.clear()
        self.write(f"echo {start}; cat {remote_path}; echo {end}\r")
        if not self.read_until(lambda b: end.encode("ascii") in b[-65536:], timeout):
            print(f"\nTIMEOUT capturing: {remote_path}", file=sys.stderr)
            return False
        raw = bytes(self.buf).decode("utf-8", errors="replace")
        start_i = raw.find(start)
        end_i = raw.find(end, start_i + len(start))
        if start_i < 0 or end_i < 0:
            print(f"capture markers missing for {remote_path}", file=sys.stderr)
            return False
        text = raw[start_i + len(start):end_i]
        text = text.replace("\r\n", "\n").replace("\r", "\n")
        if text.startswith("\n"):
            text = text[1:]
        local_path.parent.mkdir(parents=True, exist_ok=True)
        local_path.write_text(text, encoding="utf-8")
        self.read_until_prompt(timeout=3)
        return True


def shell_join(args):
    return " ".join(shlex.quote(str(a)) for a in args)


def board_run_command(label, command, remote_log):
    return (
        f"rm -f {remote_log}; "
        f"echo __{label}_START__ > {remote_log}; "
        f"printf '%s\\n' {shlex.quote('board_command: ' + command)} >> {remote_log}; "
        f"({command}) >> {remote_log} 2>&1; rc=$?; "
        f"echo __{label}_RC__=$rc >> {remote_log}; "
        f"echo __{label}_DMESG_CHECK__ >> {remote_log}; "
        "dmesg | tail -120 | grep -Ei 'panic|oops|bug:|bus error|external abort|imprecise|segfault|DMA|uio|gemv|xilinx' >> "
        f"{remote_log} 2>&1 || true; "
        f"echo __{label}_END__ >> {remote_log}; "
        f"echo __{label}_DONE__=$rc"
    )


def parse_key_values(text):
    values = {}
    for line in text.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            key = k.strip()
            if re.fullmatch(r"[A-Za-z0-9_ /.-]+", key):
                values[key] = v.strip()
    return values


def parse_block_value(text, prefix, stop_prefixes):
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.startswith(prefix):
            parts = [line[len(prefix):].strip()]
            for nxt in lines[i + 1:]:
                if any(nxt.startswith(p) for p in stop_prefixes):
                    break
                parts.append(nxt)
            return "\n".join(parts).strip()
    return ""


def parse_layer_stats_to_csv(log_text):
    pat = re.compile(
        r"layer_vector_stats: backend=(\w+) layer=(-?\d+) point=([A-Za-z0-9_]+) len=(\d+) "
        r"min=([-+0-9.eE]+) max=([-+0-9.eE]+) mean=([-+0-9.eE]+) "
        r"absmax=([-+0-9.eE]+) mean_abs=([-+0-9.eE]+) checksum=([-+0-9.eE]+) hash=([0-9a-f]+)"
    )
    stats = {}
    for m in pat.finditer(log_text):
        backend, layer, point = m.group(1), int(m.group(2)), m.group(3)
        stats[(backend, layer, point)] = {
            "len": int(m.group(4)),
            "min": float(m.group(5)),
            "max": float(m.group(6)),
            "mean": float(m.group(7)),
            "absmax": float(m.group(8)),
            "mean_abs": float(m.group(9)),
            "checksum": float(m.group(10)),
            "hash": m.group(11),
        }
    rows = []
    for backend, layer, point in list(stats):
        if backend != "cpu":
            continue
        cpu = stats[("cpu", layer, point)]
        fpga = stats.get(("fpga", layer, point))
        if not fpga:
            continue
        checksum_abs_diff = abs(cpu["checksum"] - fpga["checksum"])
        mean_abs_diff = abs(cpu["mean"] - fpga["mean"])
        absmax_abs_diff = abs(cpu["absmax"] - fpga["absmax"])
        large = (
            fpga["absmax"] >= 1000000.0 or
            checksum_abs_diff > 1024.0 or
            absmax_abs_diff > 1024.0
        )
        rows.append({
            "layer": layer,
            "point": point,
            "len": cpu["len"],
            "cpu_min": cpu["min"],
            "cpu_max": cpu["max"],
            "cpu_mean": cpu["mean"],
            "cpu_absmax": cpu["absmax"],
            "cpu_checksum": cpu["checksum"],
            "cpu_hash": cpu["hash"],
            "fpga_min": fpga["min"],
            "fpga_max": fpga["max"],
            "fpga_mean": fpga["mean"],
            "fpga_absmax": fpga["absmax"],
            "fpga_checksum": fpga["checksum"],
            "fpga_hash": fpga["hash"],
            "checksum_abs_diff": checksum_abs_diff,
            "mean_abs_diff": mean_abs_diff,
            "absmax_abs_diff": absmax_abs_diff,
            "hash_match": "yes" if cpu["hash"] == fpga["hash"] else "no",
            "large_divergence": "yes" if large else "no",
        })
    with LAYER_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "layer", "point", "len", "cpu_min", "cpu_max", "cpu_mean", "cpu_absmax",
            "cpu_checksum", "cpu_hash", "fpga_min", "fpga_max", "fpga_mean",
            "fpga_absmax", "fpga_checksum", "fpga_hash", "checksum_abs_diff",
            "mean_abs_diff", "absmax_abs_diff", "hash_match", "large_divergence",
        ])
        writer.writeheader()
        writer.writerows(rows)
    return rows


def parse_compare_backend_sections(log_text):
    sections = {}
    current = None
    buf = []
    for line in log_text.splitlines():
        if line.startswith("[compare] backend="):
            if current:
                sections[current] = "\n".join(buf)
            current = line.split("=", 1)[1].strip()
            buf = []
        elif current:
            buf.append(line)
    if current:
        sections[current] = "\n".join(buf)
    return sections


def parse_generation_section(text):
    kv = parse_key_values(text)
    top_ids = kv.get("first_token_top10_ids", "")
    ids = [int(x) for x in top_ids.split()] if top_ids else []
    top_scores = kv.get("first_token_top10_scores", "")
    generated_text = parse_block_value(text, "generated_text:", [
        "generated_text_utf8_replacements:",
        "generated_special_tokens_skipped:",
    ])
    return {
        "top10_ids": ids,
        "top10_ids_text": " ".join(str(x) for x in ids),
        "top10_scores": top_scores,
        "first_token": kv.get("first_generated_token_text", ""),
        "first_token_id": kv.get("generated_token_ids", "").split()[0] if kv.get("generated_token_ids") else "",
        "generated_text": generated_text,
        "generated_token_ids": kv.get("generated_token_ids", ""),
        "tokens_generated": kv.get("tokens_generated", ""),
        "stop_reason": kv.get("stop_reason", ""),
        "total_gemv_calls": kv.get("total_gemv_calls", ""),
        "fpga_gemv_calls": kv.get("fpga_gemv_calls", ""),
        "cpu_gemv_fallbacks": kv.get("cpu_gemv_fallbacks", ""),
        "fpga_repair_jobs": kv.get("fpga_repair_jobs", ""),
        "fpga_mode1_blockacc_calls": kv.get("fpga_mode1_blockacc_calls", ""),
        "cpu_scale_accum_ops": kv.get("cpu_scale_accum_ops", ""),
        "cpu_scale_accum_time_ms": kv.get("cpu_scale_accum_time_ms", ""),
        "s2mm_output_bytes": kv.get("s2mm_output_bytes", ""),
        "input_saturations": kv.get("input_saturations", ""),
        "fpga_time_ms": kv.get("fpga_time_ms", ""),
        "cpu_gemv_time_ms": kv.get("cpu_gemv_time_ms", ""),
        "cpu_non_gemv_time_ms": kv.get("cpu_non_gemv_time_ms", ""),
        "per_token_latency_ms": kv.get("per_token_latency_ms", ""),
    }


def write_logits_csv(log_text):
    sections = parse_compare_backend_sections(log_text)
    parsed = {name: parse_generation_section(text) for name, text in sections.items()}
    cpu_ids = parsed.get("cpu", {}).get("top10_ids", [])
    fpga_ids = parsed.get("fpga", {}).get("top10_ids", [])
    top5_overlap = len(set(cpu_ids[:5]) & set(fpga_ids[:5]))
    top10_overlap = len(set(cpu_ids[:10]) & set(fpga_ids[:10]))
    argmax_match = bool(cpu_ids and fpga_ids and cpu_ids[0] == fpga_ids[0])
    cpu_top1_in_fpga_top5 = bool(cpu_ids and cpu_ids[0] in fpga_ids[:5])
    with LOGITS_CSV.open("w", newline="", encoding="utf-8") as f:
        fields = [
            "prompt", "mode", "act_shift", "backend", "top10_ids", "top10_scores",
            "first_token_id", "generated_text", "total_gemv_calls", "fpga_gemv_calls",
            "cpu_gemv_fallbacks", "fpga_repair_jobs", "fpga_mode1_blockacc_calls",
            "cpu_scale_accum_ops", "cpu_scale_accum_time_ms", "s2mm_output_bytes",
            "input_saturations", "per_token_latency_ms", "top5_overlap",
            "top10_overlap", "argmax_match", "cpu_top1_in_fpga_top5",
        ]
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for backend in ["cpu", "fpga"]:
            row = parsed.get(backend)
            if not row:
                continue
            writer.writerow({
                "prompt": "Hi",
                "mode": "raw",
                "act_shift": 8,
                "backend": backend,
                "top10_ids": row["top10_ids_text"],
                "top10_scores": row["top10_scores"],
                "first_token_id": row["first_token_id"],
                "generated_text": row["generated_text"],
                "total_gemv_calls": row["total_gemv_calls"],
                "fpga_gemv_calls": row["fpga_gemv_calls"],
                "cpu_gemv_fallbacks": row["cpu_gemv_fallbacks"],
                "fpga_repair_jobs": row["fpga_repair_jobs"],
                "fpga_mode1_blockacc_calls": row["fpga_mode1_blockacc_calls"],
                "cpu_scale_accum_ops": row["cpu_scale_accum_ops"],
                "cpu_scale_accum_time_ms": row["cpu_scale_accum_time_ms"],
                "s2mm_output_bytes": row["s2mm_output_bytes"],
                "input_saturations": row["input_saturations"],
                "per_token_latency_ms": row["per_token_latency_ms"],
                "top5_overlap": top5_overlap,
                "top10_overlap": top10_overlap,
                "argmax_match": "yes" if argmax_match else "no",
                "cpu_top1_in_fpga_top5": "yes" if cpu_top1_in_fpga_top5 else "no",
            })
    return {
        "top5_overlap": top5_overlap,
        "top10_overlap": top10_overlap,
        "argmax_match": argmax_match,
        "cpu_top1_in_fpga_top5": cpu_top1_in_fpga_top5,
    }


def parse_role_breakdown(text):
    rows = []
    header = None
    for line in text.splitlines():
        if line.startswith("gemv_role_breakdown_header:"):
            header = [x.strip() for x in line.split(":", 1)[1].split(",")]
        elif line.startswith("gemv_role_breakdown:") and header:
            vals = [x.strip() for x in line.split(":", 1)[1].split(",")]
            rows.append(dict(zip(header, vals)))
    return rows


def parse_generation_log(run_name, prompt_mode, prompt, ctx, max_new, log_text):
    kv = parse_generation_section(log_text)
    lower = log_text.lower()
    status = "pass"
    if kv["cpu_gemv_fallbacks"] not in ("", "0"):
        status = "fail_fallback"
    if kv["fpga_repair_jobs"] not in ("", "0"):
        status = "fail_repair"
    if kv["tokens_generated"] and int(kv["tokens_generated"]) < max_new:
        status = "short_generation"
    return {
        "run": run_name,
        "backend": "fpga",
        "fpga_output_mode": "mode1_cpu_scale",
        "prompt_mode": prompt_mode,
        "prompt": prompt,
        "ctx_size": ctx,
        "max_new_tokens_requested": max_new,
        "generated_text": kv["generated_text"],
        "generated_token_ids": kv["generated_token_ids"],
        "achieved_tokens": kv["tokens_generated"],
        "stop_reason": kv["stop_reason"],
        "per_token_latency_ms": kv["per_token_latency_ms"],
        "total_gemv_calls": kv["total_gemv_calls"],
        "fpga_gemv_calls": kv["fpga_gemv_calls"],
        "cpu_gemv_fallbacks": kv["cpu_gemv_fallbacks"],
        "fpga_repair_jobs": kv["fpga_repair_jobs"],
        "fpga_mode1_blockacc_calls": kv["fpga_mode1_blockacc_calls"],
        "cpu_scale_accum_ops": kv["cpu_scale_accum_ops"],
        "cpu_scale_accum_time_ms": kv["cpu_scale_accum_time_ms"],
        "s2mm_output_bytes": kv["s2mm_output_bytes"],
        "input_saturations": kv["input_saturations"],
        "fpga_time_ms": kv["fpga_time_ms"],
        "cpu_non_gemv_time_ms": kv["cpu_non_gemv_time_ms"],
        "kernel_oops_panic": "yes" if re.search(r"panic|oops|bug:|external abort", lower) else "no",
        "status": status,
    }


def write_generation_and_perf_reports(generation_rows, perf_sources):
    gen_fields = [
        "run", "backend", "fpga_output_mode", "prompt_mode", "prompt", "ctx_size",
        "max_new_tokens_requested", "generated_text", "generated_token_ids",
        "achieved_tokens", "stop_reason", "per_token_latency_ms", "total_gemv_calls",
        "fpga_gemv_calls", "cpu_gemv_fallbacks", "fpga_repair_jobs",
        "fpga_mode1_blockacc_calls", "cpu_scale_accum_ops", "cpu_scale_accum_time_ms",
        "s2mm_output_bytes", "input_saturations", "fpga_time_ms",
        "cpu_non_gemv_time_ms", "kernel_oops_panic", "status",
    ]
    with GEN_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=gen_fields)
        writer.writeheader()
        writer.writerows(generation_rows)

    perf_fields = [
        "run", "role", "calls", "fpga_calls", "cpu_fallbacks", "fpga_rowgroup_jobs",
        "fpga_chunk_jobs", "fpga_repair_jobs", "fpga_mode1_blockacc_calls",
        "s2mm_output_bytes", "input_saturations", "fpga_time_ms", "cpu_gemv_time_ms",
        "cpu_scale_accum_ops", "cpu_scale_accum_time_ms",
    ]
    with PERF_CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=perf_fields)
        writer.writeheader()
        for run, text in perf_sources:
            for row in parse_role_breakdown(text):
                out = {"run": run}
                out.update({k: row.get(k, "") for k in perf_fields if k != "run"})
                writer.writerow(out)

    lines = [
        "# S06.5.2 Mode1 CPU Scale Performance",
        "",
        "Candidate 10 hardware was not modified. GEMV MACs run on FPGA mode1; CPU only applies Q8_0 scale and row accumulation.",
        "",
        "| run | tokens | repair jobs | CPU fallbacks | mode1 calls | CPU scale ops | S2MM bytes | ms/token | status |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in generation_rows:
        lines.append(
            f"| {row['run']} | {row['achieved_tokens']} | {row['fpga_repair_jobs']} | "
            f"{row['cpu_gemv_fallbacks']} | {row['fpga_mode1_blockacc_calls']} | "
            f"{row['cpu_scale_accum_ops']} | {row['s2mm_output_bytes']} | "
            f"{row['per_token_latency_ms']} | {row['status']} |"
        )
    lines.extend([
        "",
        "Duplicate-row repaired baseline raw 16-token latency was about 12462.203 ms/token with 169970 repair jobs.",
        "S07 functional demo gate should be judged from readable text plus zero repair jobs and zero CPU GEMV fallbacks.",
        "",
        f"Detailed role timing CSV: `{PERF_CSV}`",
    ])
    PERF_MD.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    if not SOURCE_PATH.is_file():
        print(f"missing source: {SOURCE_PATH}", file=sys.stderr)
        return 2
    source = SOURCE_PATH.read_text(encoding="ascii")
    s = Session()
    try:
        print(f"Logging serial transcript to {SERIAL_LOG}")
        s.write("\r")
        if not s.read_until_prompt(timeout=10):
            print("FAIL: Linux root prompt not detected", file=sys.stderr)
            return 2
        s.write("stty -echo\r")
        s.read_until_prompt(timeout=3)
        s.buf.clear()

        precheck = (
            "echo __S06_5_2_PRECHECK_START__;"
            "cat /proc/cmdline;"
            "uname -a;"
            "for d in /sys/class/uio/uio*; do echo UIO=$(basename \"$d\") name=$(cat \"$d/name\" 2>/dev/null); done;"
            "command -v gcc;"
            f"ls -l {MODEL_PATH};"
            "echo __S06_5_2_PRECHECK_END__"
        )
        if not s.cmd(precheck, timeout=30):
            return 3
        data = SERIAL_LOG.read_bytes()
        for required in [b"mem=960M", b"name=axi_dma", b"name=input_bram", b"name=gemv_ctrl", b"gcc", b"SmolLM2-135M-Instruct-Q8_0.gguf"]:
            if required not in data:
                print(f"FAIL: precheck missing {required.decode('ascii', 'replace')}", file=sys.stderr)
                return 3

        if not s.cmd(f"rm -f {BOARD_SOURCE} {BOARD_BIN} /tmp/s06_5_2_*", timeout=10):
            return 4
        upload = (
            f"cat > {BOARD_SOURCE} <<'__S06_5_2_SMOLLM2_CHAT_C__'\n"
            f"{source}"
            "__S06_5_2_SMOLLM2_CHAT_C__\n"
            f"wc -c {BOARD_SOURCE}; sha256sum {BOARD_SOURCE}"
        )
        if not s.cmd(upload, timeout=90):
            return 4

        compile_cmd = (
            f"gcc -O2 -Wall -Wextra -std=c11 -o {BOARD_BIN} {BOARD_SOURCE} -lm; "
            "rc=$?; echo __S06_5_2_COMPILE_RC__=$rc; "
            f"[ $rc -eq 0 ] && ls -l {BOARD_BIN} && sha256sum {BOARD_BIN}"
        )
        if not s.cmd(compile_cmd, timeout=180):
            return 5
        if b"__S06_5_2_COMPILE_RC__=0" not in SERIAL_LOG.read_bytes():
            print("FAIL: board compile failed", file=sys.stderr)
            return 5

        qproj_cmd = shell_join([
            BOARD_BIN, "--model", MODEL_PATH, "--prompt-raw", "Hi", "--act-shift", "8",
            "--compare-mode1-scaled-qproj", "--mode1-scaled-qproj-csv",
            "/tmp/s06_5_2_mode1_cpu_scale_qproj_compare.csv",
        ])
        compare_cmd = shell_join([
            BOARD_BIN, "--model", MODEL_PATH, "--compare-backends", "--require-fpga",
            "--ctx-size", "128", "--max-new-tokens", "1", "--prompt-raw", "Hi",
            "--dump-layer-stats", "--dump-top-k", "10", "--act-shift", "8",
            "--fpga-output-mode", "mode1_cpu_scale",
        ])
        layer_remote = "/tmp/s06_5_2_mode1_cpu_scale_layer_diff.txt"
        layer_cmd = f"{qproj_cmd}; echo __S06_5_2_LAYER_BACKEND_COMPARE__; {compare_cmd}"
        if not s.cmd(board_run_command("S06_5_2_LAYER", layer_cmd, layer_remote), timeout=RUN_TIMEOUT):
            return 6
        if not s.capture_file(layer_remote, LAYER_LOG, timeout=180):
            return 6
        if not s.capture_file("/tmp/s06_5_2_mode1_cpu_scale_qproj_compare.csv", QPROJ_CSV, timeout=60):
            return 6

        layer_text = LAYER_LOG.read_text(encoding="utf-8", errors="replace")
        parse_layer_stats_to_csv(layer_text)
        write_logits_csv(layer_text)

        raw_remote = "/tmp/s06_5_2_mode1_cpu_scale_raw_hello_16tok.txt"
        raw_cmd = shell_join([
            BOARD_BIN, "--model", MODEL_PATH, "--backend", "fpga", "--require-fpga",
            "--fpga-output-mode", "mode1_cpu_scale", "--ctx-size", "128",
            "--max-new-tokens", "16", "--prompt-raw", "Hello, how are you?",
            "--act-shift", "8", "--ignore-eos",
        ])
        if not s.cmd(board_run_command("S06_5_2_RAW16", raw_cmd, raw_remote), timeout=RUN_TIMEOUT):
            return 7
        if not s.capture_file(raw_remote, RAW_LOG, timeout=180):
            return 7

        chat_remote = "/tmp/s06_5_2_mode1_cpu_scale_chat_hello_16tok.txt"
        chat_cmd = shell_join([
            BOARD_BIN, "--model", MODEL_PATH, "--backend", "fpga", "--require-fpga",
            "--fpga-output-mode", "mode1_cpu_scale", "--ctx-size", "256",
            "--max-new-tokens", "16", "--prompt-chat", "Hello, how are you?",
            "--act-shift", "8", "--ignore-eos",
        ])
        if not s.cmd(board_run_command("S06_5_2_CHAT16", chat_cmd, chat_remote), timeout=RUN_TIMEOUT):
            return 8
        if not s.capture_file(chat_remote, CHAT_LOG, timeout=180):
            return 8

        generation_rows = [
            parse_generation_log("raw_hello_16", "raw", "Hello, how are you?", 128, 16,
                                 RAW_LOG.read_text(encoding="utf-8", errors="replace")),
            parse_generation_log("chat_hello_16", "chat", "Hello, how are you?", 256, 16,
                                 CHAT_LOG.read_text(encoding="utf-8", errors="replace")),
        ]
        perf_sources = [
            ("raw_hello_16", RAW_LOG.read_text(encoding="utf-8", errors="replace")),
            ("chat_hello_16", CHAT_LOG.read_text(encoding="utf-8", errors="replace")),
        ]

        if RUN_CHAT32:
            chat32_remote = "/tmp/s06_5_2_mode1_cpu_scale_chat_hello_32tok.txt"
            chat32_cmd = shell_join([
                BOARD_BIN, "--model", MODEL_PATH, "--backend", "fpga", "--require-fpga",
                "--fpga-output-mode", "mode1_cpu_scale", "--ctx-size", "256",
                "--max-new-tokens", "32", "--prompt-chat", "Hello, how are you?",
                "--act-shift", "8", "--ignore-eos",
            ])
            if s.cmd(board_run_command("S06_5_2_CHAT32", chat32_cmd, chat32_remote), timeout=RUN_TIMEOUT * 2):
                if s.capture_file(chat32_remote, CHAT32_LOG, timeout=180):
                    text = CHAT32_LOG.read_text(encoding="utf-8", errors="replace")
                    generation_rows.append(parse_generation_log(
                        "chat_hello_32", "chat", "Hello, how are you?", 256, 32, text))
                    perf_sources.append(("chat_hello_32", text))

        write_generation_and_perf_reports(generation_rows, perf_sources)
        print(f"Wrote {LAYER_LOG}")
        print(f"Wrote {LAYER_CSV}")
        print(f"Wrote {LOGITS_CSV}")
        print(f"Wrote {RAW_LOG}")
        print(f"Wrote {CHAT_LOG}")
        print(f"Wrote {GEN_CSV}")
        print(f"Wrote {PERF_CSV}")
        print(f"Wrote {PERF_MD}")
        return 0
    finally:
        try:
            s.cmd("stty echo", timeout=5)
        except Exception:
            pass
        s.close()


if __name__ == "__main__":
    sys.exit(main())
