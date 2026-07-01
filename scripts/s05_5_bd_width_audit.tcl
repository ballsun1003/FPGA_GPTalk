set script_dir [file normalize [file dirname [info script]]]
set repo_root [file normalize [file join $script_dir ..]]
set project_file [file join $repo_root hw vivado_project GPTalk.xpr]
set bd_file [file join $repo_root hw vivado_project GPTalk.srcs sources_1 bd design_1 design_1.bd]
set log_file [file join $repo_root logs s05_5_bd_width_audit.txt]

proc line {fd text} {
    puts $fd $text
}

proc prop_or_na {obj prop} {
    if {$obj eq ""} {
        return "N/A"
    }
    if {[catch {set value [get_property $prop $obj]}]} {
        return "N/A"
    }
    if {$value eq ""} {
        return "N/A"
    }
    return $value
}

proc objects_or_none {pattern} {
    set objs [get_bd_cells -quiet -hier -filter $pattern]
    if {[llength $objs] == 0} {
        return "NONE"
    }
    return [join $objs ", "]
}

proc intf_net_or_none {pin_name} {
    set pins [get_bd_intf_pins -quiet $pin_name]
    if {[llength $pins] == 0} {
        return "N/A"
    }
    set nets [get_bd_intf_nets -quiet -of_objects $pins]
    if {[llength $nets] == 0} {
        return "NONE"
    }
    return [join $nets ", "]
}

proc scalar_net_or_none {pin_name} {
    set pins [get_bd_pins -quiet $pin_name]
    if {[llength $pins] == 0} {
        return "N/A"
    }
    set nets [get_bd_nets -quiet -of_objects $pins]
    if {[llength $nets] == 0} {
        return "NONE"
    }
    return [join $nets ", "]
}

file mkdir [file dirname $log_file]
set fd [open $log_file w]

line $fd "S05.5 BD width audit"
line $fd "project: $project_file"
line $fd "bd: $bd_file"
line $fd "note: audit only; no BD regeneration, no OOC reset, no implementation launch."

if {![file exists $project_file]} {
    line $fd "ERROR: missing project"
    close $fd
    error "missing project: $project_file"
}
if {![file exists $bd_file]} {
    line $fd "ERROR: missing BD"
    close $fd
    error "missing BD: $bd_file"
}

open_project $project_file
open_bd_design $bd_file
current_bd_design design_1

line $fd ""
line $fd "cells:"
foreach cell_name {axi_dma_0 mm2s_axis_fifo s2mm_axis_fifo gemv_q8_0_dma_top_0 processing_system7_0} {
    set cell [get_bd_cells -quiet $cell_name]
    if {[llength $cell] == 0} {
        line $fd "$cell_name: MISSING"
    } else {
        line $fd "$cell_name: [prop_or_na $cell VLNV]"
    }
}

set dma [get_bd_cells -quiet axi_dma_0]
set mm2s_fifo [get_bd_cells -quiet mm2s_axis_fifo]
set s2mm_fifo [get_bd_cells -quiet s2mm_axis_fifo]
set gemv [get_bd_cells -quiet gemv_q8_0_dma_top_0]
set ps7 [get_bd_cells -quiet processing_system7_0]

line $fd ""
line $fd "stream width properties:"
line $fd "AXI DMA MM2S stream width: [prop_or_na $dma CONFIG.c_m_axis_mm2s_tdata_width]"
line $fd "AXI DMA S2MM stream width: [prop_or_na $dma CONFIG.c_s_axis_s2mm_tdata_width]"
line $fd "MM2S FIFO TDATA_NUM_BYTES: [prop_or_na $mm2s_fifo CONFIG.TDATA_NUM_BYTES]"
line $fd "MM2S FIFO HAS_TKEEP: [prop_or_na $mm2s_fifo CONFIG.HAS_TKEEP]"
line $fd "S2MM FIFO TDATA_NUM_BYTES: [prop_or_na $s2mm_fifo CONFIG.TDATA_NUM_BYTES]"
line $fd "S2MM FIFO HAS_TKEEP: [prop_or_na $s2mm_fifo CONFIG.HAS_TKEEP]"
line $fd "GEMV AXIS_DATA_WIDTH parameter: [prop_or_na $gemv CONFIG.AXIS_DATA_WIDTH]"
line $fd "GEMV LANES parameter: [prop_or_na $gemv CONFIG.LANES]"

line $fd ""
line $fd "interface nets:"
foreach pin {
    axi_dma_0/M_AXIS_MM2S
    mm2s_axis_fifo/S_AXIS
    mm2s_axis_fifo/M_AXIS
    gemv_q8_0_dma_top_0/S_AXIS
    gemv_q8_0_dma_top_0/M_AXIS
    s2mm_axis_fifo/S_AXIS
    s2mm_axis_fifo/M_AXIS
    axi_dma_0/S_AXIS_S2MM
    axi_dma_0/M_AXI_MM2S
    axi_dma_0/M_AXI_S2MM
    processing_system7_0/S_AXI_HP0
    processing_system7_0/S_AXI_HP1
} {
    line $fd "$pin -> [intf_net_or_none $pin]"
}

line $fd ""
line $fd "input BRAM Port B scalar nets:"
foreach pin {
    gemv_q8_0_dma_top_0/INPUT_BRAM_CLK
    input_vector_bram/clkb
    gemv_q8_0_dma_top_0/INPUT_BRAM_RST
    input_vector_bram/rstb
    gemv_q8_0_dma_top_0/INPUT_BRAM_EN
    input_vector_bram/enb
    gemv_q8_0_dma_top_0/INPUT_BRAM_WE
    input_vector_bram/web
    gemv_q8_0_dma_top_0/INPUT_BRAM_ADDR
    input_vector_bram/addrb
    gemv_q8_0_dma_top_0/INPUT_BRAM_DIN
    input_vector_bram/dinb
    gemv_q8_0_dma_top_0/INPUT_BRAM_DOUT
    input_vector_bram/doutb
} {
    line $fd "$pin -> [scalar_net_or_none $pin]"
}
line $fd "GEMV INPUT_BRAM_PORT interface -> [intf_net_or_none gemv_q8_0_dma_top_0/INPUT_BRAM_PORT]"
line $fd "input_vector_bram BRAM_PORTB interface -> [intf_net_or_none input_vector_bram/BRAM_PORTB]"

line $fd ""
line $fd "width/clock converter cells:"
line $fd "axis_dwidth_converter: [objects_or_none {VLNV =~ *axis_dwidth_converter*}]"
line $fd "axis_clock_converter: [objects_or_none {VLNV =~ *axis_clock_converter*}]"
line $fd "axi_clock_converter: [objects_or_none {VLNV =~ *axi_clock_converter*}]"

line $fd ""
line $fd "clock/reset properties:"
foreach prop {
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ
    CONFIG.PCW_FPGA1_PERIPHERAL_FREQMHZ
    CONFIG.PCW_FPGA2_PERIPHERAL_FREQMHZ
    CONFIG.PCW_USE_S_AXI_HP0
    CONFIG.PCW_USE_S_AXI_HP1
} {
    line $fd "processing_system7_0 $prop: [prop_or_na $ps7 $prop]"
}
foreach pin {
    processing_system7_0/FCLK_CLK0
    processing_system7_0/FCLK_CLK1
    processing_system7_0/FCLK_CLK2
    processing_system7_0/S_AXI_HP0_ACLK
    processing_system7_0/S_AXI_HP1_ACLK
    axi_dma_0/m_axi_mm2s_aclk
    axi_dma_0/m_axi_s2mm_aclk
    axi_dma_0/m_axi_sg_aclk
    axi_dma_0/s_axi_lite_aclk
    mm2s_axis_fifo/s_axis_aclk
    mm2s_axis_fifo/m_axis_aclk
    s2mm_axis_fifo/s_axis_aclk
    s2mm_axis_fifo/m_axis_aclk
    gemv_q8_0_dma_top_0/S_AXI_ACLK
} {
    set bd_pin [get_bd_pins -quiet $pin]
    if {[llength $bd_pin] == 0} {
        line $fd "$pin net: N/A"
    } else {
        set nets [get_bd_nets -quiet -of_objects $bd_pin]
        if {[llength $nets] == 0} {
            line $fd "$pin net: NONE"
        } else {
            line $fd "$pin net: [join $nets {, }]"
        }
    }
}

line $fd ""
line $fd "OOC/run status:"
foreach run [get_runs -quiet] {
    set run_name [get_property NAME $run]
    if {[regexp {(gemv|axis_fifo|axi_dma)} $run_name]} {
        line $fd "$run_name: [prop_or_na $run STATUS]"
    }
}

close $fd
puts "S05_5_BD_WIDTH_AUDIT_LOG=$log_file"
close_project
