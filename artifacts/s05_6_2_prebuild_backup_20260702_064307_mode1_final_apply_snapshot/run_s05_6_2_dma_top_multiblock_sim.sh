#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${VIVADO_ROOT:-}" ]]; then
    if command -v vivado >/dev/null 2>&1; then
        VIVADO_ROOT="$(cd "$(dirname "$(command -v vivado)")/.." && pwd)"
    elif [[ -n "${XILINX_VIVADO:-}" ]]; then
        VIVADO_ROOT="$XILINX_VIVADO"
    else
        echo "Set VIVADO_ROOT or put vivado on PATH" >&2
        exit 1
    fi
fi

VIVADO_BIN="$VIVADO_ROOT/bin"
VIVADO_LIB="$VIVADO_ROOT/lib/lnx64.o"
XSIM_GCC_DIR="$VIVADO_ROOT/tps/lnx64/gcc-9.3.0/bin"
GCC_REDIRECT_SO="$ROOT/logs/xsim_gcc_redirect.so"
RUN_ROOT="$ROOT/logs/s05_6_2_dma_top_xsim_$(date +%Y%m%d_%H%M%S)"
SUMMARY="$ROOT/logs/s05_6_2_dma_top_multiblock_sim.txt"
TOP="tb_gemv_q8_0_dma_top_multiblock"
SNAPSHOT="s05_6_2_dma_top_multiblock_behav"
CASE_DIR="$ROOT/golden/s05_6_1_multiblock/B_64x16_P0"

mkdir -p "$RUN_ROOT" "$ROOT/logs" "$ROOT/reports"
if [[ ! -d "$CASE_DIR" ]]; then
    python3 "$ROOT/scripts/s05_6_1_multiblock_reference.py" > "$ROOT/logs/s05_6_2_reference_regen.log" 2>&1
fi

PRJ="$RUN_ROOT/s05_6_2_dma_top_multiblock.prj"
cat > "$PRJ" <<EOF
sv xil_defaultlib "$ROOT/vivado_ip/rtl/gemv_q8_0_ctrl_axi_lite.v"
sv xil_defaultlib "$ROOT/vivado_ip/rtl/gemv_q8_0_stream_core.v"
sv xil_defaultlib "$ROOT/vivado_ip/rtl/gemv_q8_0_dma_top.v"
sv xil_defaultlib "$ROOT/vivado_ip/tb/tb_gemv_q8_0_dma_top_multiblock.sv"
verilog xil_defaultlib "$VIVADO_ROOT/data/verilog/src/glbl.v"
nosort
EOF
cat > "$RUN_ROOT/xsim.ini" <<EOF
uvm=$VIVADO_ROOT/data/xsim/system_verilog/uvm
xil_defaultlib=xsim.dir/xil_defaultlib
EOF

{
    echo "S05.6.2 dma_top wrapper multi-block simulation"
    echo "run_dir: $RUN_ROOT"
    echo "case_dir: $CASE_DIR"
    echo "source_hash_stream_core: $(sha256sum "$ROOT/vivado_ip/rtl/gemv_q8_0_stream_core.v" | awk '{print $1}')"
    echo "source_hash_dma_top: $(sha256sum "$ROOT/vivado_ip/rtl/gemv_q8_0_dma_top.v" | awk '{print $1}')"
    echo
} > "$SUMMARY"

(
    cd "$RUN_ROOT"
    "$VIVADO_BIN/xvlog" --incr --relax -L uvm -prj "$PRJ" > compile.log 2>&1
    env \
        RDI_DATADIR="$VIVADO_ROOT/data" \
        RDI_APPROOT="$VIVADO_ROOT" \
        RDI_BINROOT="$VIVADO_BIN" \
        RDI_BINDIR="$VIVADO_BIN" \
        RDI_LIBDIR="$VIVADO_LIB" \
        RDI_BASEROOT="$(dirname "$VIVADO_ROOT")" \
        RDI_INSTALLROOT="$(dirname "$(dirname "$VIVADO_ROOT")")" \
        RDI_INSTALLVER="$(basename "$VIVADO_ROOT")" \
        LD_LIBRARY_PATH="$VIVADO_LIB" \
        LD_PRELOAD="$GCC_REDIRECT_SO" \
        PATH="$XSIM_GCC_DIR:$VIVADO_BIN:$PATH" \
        "$VIVADO_BIN/unwrapped/lnx64.o/xelab" \
            --incr --debug typical --relax --mt 8 \
            -L xil_defaultlib -L uvm -L unisims_ver -L unimacro_ver -L secureip \
            --snapshot "$SNAPSHOT" "xil_defaultlib.$TOP" xil_defaultlib.glbl \
            -log elaborate.log > elaborate_stdout.log 2>&1
)

status=0
SIM_LOG="$RUN_ROOT/B_64x16_P0_dma_top.log"
(
    cd "$RUN_ROOT"
    "$VIVADO_BIN/xsim" "$SNAPSHOT" \
        --runall \
        --testplusarg "CASE_DIR=$CASE_DIR" \
        --testplusarg "BRAM_LATENCY=2" \
        --log "$SIM_LOG" > "${SIM_LOG%.log}_stdout.log" 2>&1
) || status=1

if grep -q "S05_6_2_DMA_TOP_MULTIBLOCK_PASS" "$SIM_LOG"; then
    echo "PASS,B_64x16_P0,mode0_mode1,bram_latency=2,backpressure=on" >> "$SUMMARY"
else
    echo "FAIL,B_64x16_P0,mode0_mode1,bram_latency=2,backpressure=on" >> "$SUMMARY"
    tail -80 "$SIM_LOG" >> "$SUMMARY"
    status=1
fi

grep -E 'LANE_FOCUS|STATUS|S05_6_2_DMA_TOP_MULTIBLOCK' "$SIM_LOG" >> "$SUMMARY" || true

if [[ "$status" -eq 0 ]]; then
    echo "S05_6_2_DMA_TOP_WRAPPER_SIM_PASS" >> "$SUMMARY"
else
    echo "S05_6_2_DMA_TOP_WRAPPER_SIM_FAIL" >> "$SUMMARY"
fi

exit "$status"
