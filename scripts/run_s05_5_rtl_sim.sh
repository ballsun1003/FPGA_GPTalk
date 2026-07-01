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
GOLDEN_DIR="$ROOT/pycharm/golden/fake_gemv"

run_width() {
    local width="$1"
    local run_dir="$ROOT/logs/s05_5_xsim_${width}_$(date +%Y%m%d_%H%M%S)"
    local prj="$run_dir/s05_5_axis_${width}.prj"
    local top="tb_gemv_q8_0_stream_core_axis_width"
    local snapshot="s05_5_axis${width}_behav"

    mkdir -p "$run_dir"
    cat > "$prj" <<EOF
sv xil_defaultlib -d AXIS_TDATA_WIDTH=${width} "$ROOT/vivado_ip/rtl/gemv_q8_0_stream_core.v"
sv xil_defaultlib -d AXIS_TDATA_WIDTH=${width} "$ROOT/vivado_ip/tb/tb_gemv_q8_0_stream_core_axis_width.sv"
verilog xil_defaultlib "$VIVADO_ROOT/data/verilog/src/glbl.v"
nosort
EOF
    cat > "$run_dir/xsim.ini" <<EOF
uvm=$VIVADO_ROOT/data/xsim/system_verilog/uvm
xil_defaultlib=xsim.dir/xil_defaultlib
EOF

    (
        cd "$run_dir"
        "$VIVADO_BIN/xvlog" --incr --relax -L uvm -prj "$prj" > compile.log 2>&1
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
                --snapshot "$snapshot" "xil_defaultlib.$top" xil_defaultlib.glbl \
                -log elaborate.log > elaborate_stdout.log 2>&1
        "$VIVADO_BIN/xsim" "$snapshot" \
            --runall \
            --testplusarg "GOLDEN_DIR=$GOLDEN_DIR" \
            --log simulate.log > simulate_stdout.log 2>&1
    )
    cp "$run_dir/simulate.log" "$ROOT/logs/s05_5_rtl_sim_${width}.txt"
}

run_width 32
run_width 128
