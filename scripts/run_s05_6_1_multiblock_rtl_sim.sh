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
RUN_ROOT="$ROOT/logs/s05_6_1_xsim_$(date +%Y%m%d_%H%M%S)"
SUMMARY="$ROOT/logs/s05_6_1_multiblock_rtl_sim.txt"
CSV="$ROOT/reports/s05_6_1_multiblock_expected.csv"
TOP="tb_gemv_q8_0_stream_core_multiblock"
SNAPSHOT="s05_6_1_multiblock_behav"

mkdir -p "$RUN_ROOT" "$ROOT/logs" "$ROOT/reports"
python3 "$ROOT/scripts/s05_6_1_multiblock_reference.py" > "$ROOT/logs/s05_6_1_multiblock_reference_run.log" 2>&1

PRJ="$RUN_ROOT/s05_6_1_multiblock.prj"
cat > "$PRJ" <<EOF
sv xil_defaultlib "$ROOT/vivado_ip/rtl/gemv_q8_0_stream_core.v"
sv xil_defaultlib "$ROOT/vivado_ip/tb/tb_gemv_q8_0_stream_core_multiblock.sv"
verilog xil_defaultlib "$VIVADO_ROOT/data/verilog/src/glbl.v"
nosort
EOF
cat > "$RUN_ROOT/xsim.ini" <<EOF
uvm=$VIVADO_ROOT/data/xsim/system_verilog/uvm
xil_defaultlib=xsim.dir/xil_defaultlib
EOF

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

{
    echo "S05.6.1 multi-block RTL simulation"
    echo "run_dir: $RUN_ROOT"
    echo "csv: $CSV"
    echo
} > "$SUMMARY"

status=0
while IFS=, read -r case_name shape pattern in_features out_features blocks row_groups packet_bytes axis128_beats tlast dma_limit dma_ok output0 output15 golden_dir; do
    [[ "$case_name" == "case" ]] && continue
    [[ "$golden_dir" == DEFER* ]] && {
        echo "SKIP,$case_name,$shape,$pattern,$golden_dir" >> "$SUMMARY"
        continue
    }
    case_log="$RUN_ROOT/${case_name}.log"
    case_cfg="$RUN_ROOT/${case_name}.cfg"
    {
        echo "$case_name"
        echo "$ROOT/$golden_dir"
        echo "$in_features"
        echo "$out_features"
        echo "$packet_bytes"
    } > "$case_cfg"
    (
        cd "$RUN_ROOT"
        "$VIVADO_BIN/xsim" "$SNAPSHOT" \
            --runall \
            --testplusarg "CASE_CFG=$case_cfg" \
            --log "$case_log" > "${case_log%.log}_stdout.log" 2>&1
    ) || status=1
    if grep -q "S05_6_1_MULTIBLOCK_CASE_PASS" "$case_log"; then
        echo "PASS,$case_name,$shape,$pattern,$packet_bytes,$axis128_beats,$tlast" >> "$SUMMARY"
    else
        echo "FAIL,$case_name,$shape,$pattern,$packet_bytes,$axis128_beats,$tlast" >> "$SUMMARY"
        tail -40 "$case_log" >> "$SUMMARY"
        status=1
        break
    fi
    mode1_log="$RUN_ROOT/${case_name}_mode1.log"
    (
        cd "$RUN_ROOT"
        "$VIVADO_BIN/xsim" "$SNAPSHOT" \
            --runall \
            --testplusarg "CASE_CFG=$case_cfg" \
            --testplusarg "MODE1_BLOCKS" \
            --log "$mode1_log" > "${mode1_log%.log}_stdout.log" 2>&1
    ) || status=1
    if grep -q "S05_6_1_MULTIBLOCK_CASE_PASS" "$mode1_log"; then
        echo "PASS_MODE1,$case_name,$shape,$pattern,$packet_bytes,$axis128_beats,$tlast" >> "$SUMMARY"
    else
        echo "FAIL_MODE1,$case_name,$shape,$pattern,$packet_bytes,$axis128_beats,$tlast" >> "$SUMMARY"
        tail -40 "$mode1_log" >> "$SUMMARY"
        status=1
        break
    fi
done < "$CSV"

if [[ "$status" -eq 0 ]]; then
    echo "S05_6_1_RTL_SIM_PASS" >> "$SUMMARY"
else
    echo "S05_6_1_RTL_SIM_FAIL" >> "$SUMMARY"
fi

exit "$status"
