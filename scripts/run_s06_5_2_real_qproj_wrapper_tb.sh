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
RUN_ROOT="$ROOT/logs/s06_5_2_real_qproj_xsim_$(date +%Y%m%d_%H%M%S)"
SUMMARY="$ROOT/logs/s06_5_2_real_qproj_wrapper_tb.txt"
SIM_CSV="$ROOT/reports/s06_5_2_real_qproj_wrapper_tb_sim.csv"
TOP="tb_gemv_q8_0_dma_top_multiblock"
SNAPSHOT="s06_5_2_real_qproj_dma_top_behav"
FIXTURE_ROOT="$ROOT/golden/s06_5_2_real_qproj_wrapper_tb"
CASE_SUMMARY="$FIXTURE_ROOT/case_summary.csv"

mkdir -p "$RUN_ROOT" "$ROOT/logs" "$ROOT/reports"
if [[ ! -f "$CASE_SUMMARY" ]]; then
    python3 "$ROOT/scripts/s06_5_2_prepare_real_qproj_wrapper_tb.py"
fi

PRJ="$RUN_ROOT/s06_5_2_real_qproj_dma_top.prj"
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
    echo "S06.5.2 real q_proj wrapper TB simulation"
    echo "run_dir: $RUN_ROOT"
    echo "fixture_root: $FIXTURE_ROOT"
    echo "vivado_build: not_run"
    echo "bitstream_boot_xsa_modified: no"
    echo "source_hash_stream_core: $(sha256sum "$ROOT/vivado_ip/rtl/gemv_q8_0_stream_core.v" | awk '{print $1}')"
    echo "source_hash_dma_top: $(sha256sum "$ROOT/vivado_ip/rtl/gemv_q8_0_dma_top.v" | awk '{print $1}')"
    echo "source_hash_tb: $(sha256sum "$ROOT/vivado_ip/tb/tb_gemv_q8_0_dma_top_multiblock.sv" | awk '{print $1}')"
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

echo "case,row_group,focus_lane,result,sim_log" > "$SIM_CSV"
status=0
while IFS=, read -r case_name row_group affected_lanes _affected_rows in_features out_features _blocks packet_bytes _beats _tlast _identity_scale_q fixture_dir; do
    [[ "$case_name" == "case" ]] && continue
    focus_lane="${affected_lanes%%|*}"
    sim_log="$RUN_ROOT/${case_name}.log"
    set +e
    (
        cd "$RUN_ROOT"
        "$VIVADO_BIN/xsim" "$SNAPSHOT" \
            --runall \
            --testplusarg "CASE_NAME=$case_name" \
            --testplusarg "CASE_DIR=$fixture_dir" \
            --testplusarg "IN_FEATURES=$in_features" \
            --testplusarg "OUT_FEATURES=$out_features" \
            --testplusarg "PACKET_BYTES=$packet_bytes" \
            --testplusarg "BRAM_LATENCY=2" \
            --testplusarg "FOCUS_LANE=$focus_lane" \
            --log "$sim_log" > "${sim_log%.log}_stdout.log" 2>&1
    )
    rc=$?
    set -e
    if [[ "$rc" -eq 0 ]] && grep -q "S05_6_2_DMA_TOP_MULTIBLOCK_PASS" "$sim_log"; then
        result="PASS"
    else
        result="FAIL"
        status=1
    fi
    echo "$result,$case_name,row_group=$row_group,focus_lane=$focus_lane" >> "$SUMMARY"
    grep -E 'LANE_FOCUS|STATUS|MULTIBLOCK_(PASS|FAIL)|\[FAIL\]' "$sim_log" >> "$SUMMARY" || true
    echo "$case_name,$row_group,$focus_lane,$result,$sim_log" >> "$SIM_CSV"
done < "$CASE_SUMMARY"

if [[ "$status" -eq 0 ]]; then
    echo "S06_5_2_REAL_QPROJ_WRAPPER_TB_PASS" >> "$SUMMARY"
else
    echo "S06_5_2_REAL_QPROJ_WRAPPER_TB_FAIL" >> "$SUMMARY"
fi

exit "$status"
