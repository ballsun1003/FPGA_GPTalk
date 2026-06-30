# Hardware preflight for the active GPTalk Vivado project.
# Opens only hw/vivado_project/GPTalk.xpr and writes logs under logs/.

set script_dir [file normalize [file dirname [info script]]]
set repo_root [file normalize [file join $script_dir ..]]
set project_xpr [file join $repo_root hw vivado_project GPTalk.xpr]
set log_dir [file join $repo_root logs]
file mkdir $log_dir

set bd_validate_log [file join $log_dir hw_preflight_bd_validate.log]
set clock_reset_log [file join $log_dir hw_preflight_clock_reset.txt]
set address_log [file join $log_dir hw_dma_address_map.txt]
set summary_log [file join $log_dir hw_preflight_summary.txt]

proc write_file {path text} {
    set fd [open $path w]
    puts -nonewline $fd $text
    close $fd
}

proc append_line {var_name text} {
    upvar 1 $var_name out
    append out "$text\n"
}

proc names_or_missing {objects} {
    if {[llength $objects] == 0} {
        return "MISSING"
    }
    set names {}
    foreach obj $objects {
        lappend names [get_property NAME $obj]
    }
    return [join $names ", "]
}

if {![file exists $project_xpr]} {
    error "Active Vivado project missing: $project_xpr"
}

open_project $project_xpr
set bd_files [get_files -quiet -regexp ".*/bd/design_1/design_1\\.bd$"]
if {[llength $bd_files] != 1} {
    error "Expected one design_1.bd, got [llength $bd_files]: $bd_files"
}
open_bd_design [lindex $bd_files 0]

set validate_text ""
append_line validate_text "Active project: $project_xpr"
append_line validate_text "BD file: [lindex $bd_files 0]"
if {[catch {validate_bd_design} validate_msg]} {
    append_line validate_text "BD validate: FAIL"
    append_line validate_text "$validate_msg"
} else {
    append_line validate_text "BD validate: PASS"
    append_line validate_text "$validate_msg"
}
write_file $bd_validate_log $validate_text

set addr_text "GPTalk hardware preflight address map\n"
append_line addr_text "Active Vivado project: $project_xpr"
foreach seg [lsort [get_bd_addr_segs -quiet -hier]] {
    set offset [get_property OFFSET $seg]
    set range [get_property RANGE $seg]
    if {$offset ne "" && $range ne ""} {
        append_line addr_text "[get_property NAME $seg] PATH=$seg OFFSET=$offset RANGE=$range"
    }
}
write_file $address_log $addr_text

set cr_text "GPTalk clock/reset preflight\n"
append_line cr_text "Active Vivado project: $project_xpr"
foreach cell_name [list processing_system7_0 axi_dma_0 mm2s_axis_fifo s2mm_axis_fifo gemv_q8_0_dma_top_0 axi_gemv_hp1_smc axi_video_hp0_smc axi_ctrl_smc axi_dma_ctrl_clkconv axi_gemv_ctrl_clkconv axi_bram_ctrl_clkconv rst_ps7_0_100M rst_ps7_0_133M rst_ps7_0_gemv] {
    set cells [get_bd_cells -quiet $cell_name]
    append_line cr_text "cell $cell_name: [expr {[llength $cells] ? "PRESENT" : "MISSING"}]"
}
append_line cr_text ""
append_line cr_text "Clock nets:"
foreach net_name [list processing_system7_0_FCLK_CLK0 processing_system7_0_FCLK_CLK1 processing_system7_0_FCLK_CLK2] {
    set nets [get_bd_nets -quiet $net_name]
    append_line cr_text "$net_name: [names_or_missing $nets]"
    if {[llength $nets]} {
        foreach pin [lsort [get_bd_pins -quiet -of_objects [lindex $nets 0]]] {
            append_line cr_text "  [get_property NAME $pin]"
        }
    }
}
append_line cr_text ""
append_line cr_text "Reset nets:"
foreach net_name [list processing_system7_0_FCLK_RESET0_N rst_ps7_0_100M_peripheral_aresetn rst_ps7_0_133M_peripheral_aresetn rst_ps7_0_gemv_peripheral_aresetn] {
    set nets [get_bd_nets -quiet $net_name]
    append_line cr_text "$net_name: [names_or_missing $nets]"
    if {[llength $nets]} {
        foreach pin [lsort [get_bd_pins -quiet -of_objects [lindex $nets 0]]] {
            append_line cr_text "  [get_property NAME $pin]"
        }
    }
}
write_file $clock_reset_log $cr_text

set summary "GPTalk hardware preflight summary\n"
append_line summary "Active project: $project_xpr"
append_line summary "Part: [get_property PART [current_project]]"
append_line summary "Board part: [get_property BOARD_PART [current_project]]"
append_line summary "Top: [get_property top [current_fileset]]"
append_line summary ""
append_line summary "Required cells:"
foreach cell_name [list processing_system7_0 axi_dma_0 mm2s_axis_fifo s2mm_axis_fifo gemv_q8_0_dma_top_0 axi_input_bram_ctrl] {
    append_line summary "- $cell_name: [expr {[llength [get_bd_cells -quiet $cell_name]] ? "PRESENT" : "MISSING"}]"
}
append_line summary ""
append_line summary "Required interface nets:"
foreach intf_net [list processing_system7_0_M_AXI_GP0 axi_dma_0_M_AXIS_MM2S mm2s_axis_fifo_M_AXIS gemv_q8_0_dma_top_0_M_AXIS s2mm_axis_fifo_M_AXIS axi_dma_0_M_AXI_MM2S axi_dma_0_M_AXI_S2MM axi_gemv_hp1_smc_M00_AXI axi_vdma_1_M_AXI_MM2S] {
    append_line summary "- $intf_net: [names_or_missing [get_bd_intf_nets -quiet $intf_net]]"
}
append_line summary ""
append_line summary "PS7 selected properties:"
set ps [get_bd_cells -quiet processing_system7_0]
if {[llength $ps]} {
    foreach prop [list CONFIG.PCW_UIPARAM_DDR_PARTNO CONFIG.PCW_UIPARAM_DDR_FREQ_MHZ CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH CONFIG.PCW_EN_SDIO0 CONFIG.PCW_SD0_SD0_IO CONFIG.PCW_EN_UART1 CONFIG.PCW_UART1_UART1_IO CONFIG.PCW_USE_M_AXI_GP0 CONFIG.PCW_USE_S_AXI_HP0 CONFIG.PCW_USE_S_AXI_HP1 CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ CONFIG.PCW_FPGA1_PERIPHERAL_FREQMHZ CONFIG.PCW_FPGA2_PERIPHERAL_FREQMHZ CONFIG.PCW_CPU_CPU_6X4X_MAX_RANGE] {
        set value "<unavailable>"
        catch {set value [get_property $prop $ps]}
        append_line summary "- $prop = $value"
    }
}
write_file $summary_log $summary

close_project
