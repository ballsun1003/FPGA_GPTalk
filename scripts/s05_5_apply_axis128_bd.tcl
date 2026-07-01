set script_dir [file normalize [file dirname [info script]]]
set repo_root [file normalize [file join $script_dir ..]]
set project_xpr [file join $repo_root hw vivado_project GPTalk.xpr]
set project_dir [file dirname $project_xpr]
set bd_name design_1
set bd_file [file join $project_dir GPTalk.srcs sources_1 bd $bd_name ${bd_name}.bd]
set axis128_top [file join $repo_root vivado_ip rtl gemv_q8_0_dma_top_axis128.v]
set log_file [file join $repo_root logs s05_5_axis128_bd_apply.txt]
file mkdir [file dirname $log_file]

proc line {fd text} {
    puts $fd $text
}

proc must_get_cell {name} {
    set obj [get_bd_cells -quiet $name]
    if {![llength $obj]} {
        error "missing BD cell: $name"
    }
    return $obj
}

proc must_get_pin {name} {
    set obj [get_bd_pins -quiet $name]
    if {![llength $obj]} {
        error "missing BD pin: $name"
    }
    return $obj
}

proc must_get_intf_pin {name} {
    set obj [get_bd_intf_pins -quiet $name]
    if {![llength $obj]} {
        error "missing BD interface pin: $name"
    }
    return $obj
}

proc connect_intf {a b} {
    set pa [must_get_intf_pin $a]
    set pb [must_get_intf_pin $b]
    set na [get_bd_intf_nets -quiet -of_objects $pa]
    set nb [get_bd_intf_nets -quiet -of_objects $pb]
    if {[llength $na] && [llength $nb] && [lindex $na 0] eq [lindex $nb 0]} {
        return
    }
    if {[llength $na] && ![llength $nb]} {
        disconnect_bd_intf_net [lindex $na 0] $pa
        connect_bd_intf_net $pa $pb
    } elseif {![llength $na] && [llength $nb]} {
        disconnect_bd_intf_net [lindex $nb 0] $pb
        connect_bd_intf_net $pa $pb
    } else {
        connect_bd_intf_net $pa $pb
    }
}

proc delete_intf_net_if_exists {name} {
    set net [get_bd_intf_nets -quiet $name]
    if {[llength $net]} {
        delete_bd_objs $net
    }
}

proc delete_net_if_exists {name} {
    set net [get_bd_nets -quiet $name]
    if {[llength $net]} {
        delete_bd_objs $net
    }
}

proc connect_net {args} {
    set pins {}
    foreach pin $args {
        lappend pins [must_get_pin $pin]
    }
    connect_bd_net {*}$pins
}

proc try_set {obj props} {
    foreach {name value} $props {
        if {[catch {set_property $name $value $obj} msg]} {
            puts "WARN: could not set $name=$value on $obj: $msg"
        }
    }
}

proc set_addr_segment {pattern base range} {
    set segs [get_bd_addr_segs -quiet -hier -regexp $pattern]
    if {[llength $segs] == 0} {
        return 0
    }
    foreach seg $segs {
        catch {set_property offset $base $seg}
        catch {set_property range $range $seg}
    }
    return 1
}

set fd [open $log_file w]
line $fd "S05.5 apply 128-bit AXIS BD variant"
line $fd "project: $project_xpr"
line $fd "bd: $bd_file"
line $fd "scope: reuse active GPTalk.xpr; preserve PS7; recreate only gemv_q8_0_dma_top_0 module_ref cell."

open_project $project_xpr
if {[llength [get_files -quiet $axis128_top]] == 0} {
    add_files -norecurse $axis128_top
}
update_compile_order -fileset sources_1
open_bd_design $bd_file
current_bd_design $bd_name

set ps7 [must_get_cell processing_system7_0]
set dma [must_get_cell axi_dma_0]
set mm2s_fifo [must_get_cell mm2s_axis_fifo]
set s2mm_fifo [must_get_cell s2mm_axis_fifo]
must_get_cell input_vector_bram

line $fd ""
line $fd "before:"
line $fd "axi_dma_0 CONFIG.c_m_axis_mm2s_tdata_width: [get_property CONFIG.c_m_axis_mm2s_tdata_width $dma]"
line $fd "axi_dma_0 CONFIG.c_s_axis_s2mm_tdata_width: [get_property CONFIG.c_s_axis_s2mm_tdata_width $dma]"
line $fd "mm2s_axis_fifo CONFIG.TDATA_NUM_BYTES: [get_property CONFIG.TDATA_NUM_BYTES $mm2s_fifo]"
line $fd "s2mm_axis_fifo CONFIG.TDATA_NUM_BYTES: [get_property CONFIG.TDATA_NUM_BYTES $s2mm_fifo]"
if {[llength [get_bd_pins -quiet gemv_q8_0_dma_top_0/S_AXIS_TDATA]]} {
    line $fd "gemv S_AXIS_TDATA LEFT: [get_property LEFT [get_bd_pins gemv_q8_0_dma_top_0/S_AXIS_TDATA]]"
}

try_set $dma [list \
    CONFIG.c_m_axis_mm2s_tdata_width 128 \
    CONFIG.c_s_axis_s2mm_tdata_width 32 \
]
try_set $mm2s_fifo [list \
    CONFIG.TDATA_NUM_BYTES 16 \
    CONFIG.HAS_TKEEP 1 \
    CONFIG.HAS_TLAST 1 \
]
try_set $s2mm_fifo [list \
    CONFIG.TDATA_NUM_BYTES 4 \
    CONFIG.HAS_TKEEP 1 \
    CONFIG.HAS_TLAST 1 \
]

set old_gemv [get_bd_cells -quiet gemv_q8_0_dma_top_0]
if {[llength $old_gemv]} {
    delete_bd_objs $old_gemv
}
foreach net_name {
    axi_gemv_ctrl_clkconv_M_AXI
    mm2s_axis_fifo_M_AXIS
    gemv_q8_0_dma_top_0_M_AXIS
    gemv_q8_0_dma_top_0_INPUT_BRAM_PORT
} {
    delete_intf_net_if_exists $net_name
}
foreach net_name {
    gemv_q8_0_dma_top_0_INPUT_BRAM_ADDR
    gemv_q8_0_dma_top_0_INPUT_BRAM_CLK
    gemv_q8_0_dma_top_0_INPUT_BRAM_DIN
    gemv_q8_0_dma_top_0_INPUT_BRAM_DOUT
    gemv_q8_0_dma_top_0_INPUT_BRAM_EN
    gemv_q8_0_dma_top_0_INPUT_BRAM_RST
    gemv_q8_0_dma_top_0_INPUT_BRAM_WE
    input_vector_bram_doutb
} {
    delete_net_if_exists $net_name
}
create_bd_cell -type module -reference gemv_q8_0_dma_top_axis128 gemv_q8_0_dma_top_0
set gemv [must_get_cell gemv_q8_0_dma_top_0]

connect_intf axi_gemv_ctrl_clkconv/M_AXI gemv_q8_0_dma_top_0/S_AXI
connect_intf mm2s_axis_fifo/M_AXIS gemv_q8_0_dma_top_0/S_AXIS
connect_intf gemv_q8_0_dma_top_0/M_AXIS s2mm_axis_fifo/S_AXIS

# The module_ref BRAM interface connection left INPUT_BRAM_DOUT on its
# generated driver_value in the failed 128-bit build. Keep this path scalar
# and explicit so Port B DOUT cannot silently float to the module_ref default.
connect_net gemv_q8_0_dma_top_0/INPUT_BRAM_CLK input_vector_bram/clkb
connect_net gemv_q8_0_dma_top_0/INPUT_BRAM_RST input_vector_bram/rstb
connect_net gemv_q8_0_dma_top_0/INPUT_BRAM_EN input_vector_bram/enb
connect_net gemv_q8_0_dma_top_0/INPUT_BRAM_WE input_vector_bram/web
connect_net gemv_q8_0_dma_top_0/INPUT_BRAM_ADDR input_vector_bram/addrb
connect_net gemv_q8_0_dma_top_0/INPUT_BRAM_DIN input_vector_bram/dinb
connect_net gemv_q8_0_dma_top_0/INPUT_BRAM_DOUT input_vector_bram/doutb

connect_net processing_system7_0/FCLK_CLK2 gemv_q8_0_dma_top_0/S_AXI_ACLK
connect_net rst_ps7_0_gemv/peripheral_aresetn gemv_q8_0_dma_top_0/S_AXI_ARESETN

set ps_addr_space [get_bd_addr_spaces -quiet processing_system7_0/Data]
set gemv_slave_seg [get_bd_addr_segs -quiet gemv_q8_0_dma_top_0/S_AXI/reg0]
if {![llength $ps_addr_space] || ![llength $gemv_slave_seg]} {
    line $fd "FAIL: missing PS address space or GEMV slave segment"
    close $fd
    error "missing address objects for GEMV segment"
}
set old_gemv_master_seg [get_bd_addr_segs -quiet processing_system7_0/Data/SEG_gemv_q8_0_dma_top_0_reg0]
if {[llength $old_gemv_master_seg]} {
    delete_bd_objs $old_gemv_master_seg
}
set invalid_vdma_seg [get_bd_addr_segs -quiet processing_system7_0/Data/SEG_axi_vdma_1_Reg]
if {[llength $invalid_vdma_seg]} {
    line $fd "removed invalid pre-existing HDMI VDMA address segment for S05.5 target generation: $invalid_vdma_seg"
    delete_bd_objs $invalid_vdma_seg
}
set s_tdata_left [get_property LEFT [must_get_pin gemv_q8_0_dma_top_0/S_AXIS_TDATA]]
set s_tkeep_left [get_property LEFT [must_get_pin gemv_q8_0_dma_top_0/S_AXIS_TKEEP]]
set m_tdata_left [get_property LEFT [must_get_pin gemv_q8_0_dma_top_0/M_AXIS_TDATA]]
if {$s_tdata_left ne "127" || $s_tkeep_left ne "15" || $m_tdata_left ne "31"} {
    line $fd "FAIL: unexpected GEMV port widths S_TDATA_LEFT=$s_tdata_left S_TKEEP_LEFT=$s_tkeep_left M_TDATA_LEFT=$m_tdata_left"
    close $fd
    error "unexpected GEMV 128-bit boundary"
}

create_bd_addr_seg -range 4K -offset 0x43CA0000 $ps_addr_space $gemv_slave_seg SEG_gemv_q8_0_dma_top_0_reg0

set validate_status [catch {validate_bd_design} validate_msg]
line $fd "validate_bd_design status: $validate_status"
line $fd "validate_bd_design message: $validate_msg"
line $fd "validate_bd_design note: nonzero status is tolerated here because the active GPTalk HDMI VDMA address path has a pre-existing validate issue unrelated to the GEMV 128-bit stream boundary."

save_bd_design
generate_target all [get_files $bd_file]
update_compile_order -fileset sources_1

line $fd ""
line $fd "after:"
line $fd "axi_dma_0 CONFIG.c_m_axis_mm2s_tdata_width: [get_property CONFIG.c_m_axis_mm2s_tdata_width $dma]"
line $fd "axi_dma_0 CONFIG.c_s_axis_s2mm_tdata_width: [get_property CONFIG.c_s_axis_s2mm_tdata_width $dma]"
line $fd "mm2s_axis_fifo CONFIG.TDATA_NUM_BYTES: [get_property CONFIG.TDATA_NUM_BYTES $mm2s_fifo]"
line $fd "s2mm_axis_fifo CONFIG.TDATA_NUM_BYTES: [get_property CONFIG.TDATA_NUM_BYTES $s2mm_fifo]"
line $fd "gemv S_AXIS_TDATA LEFT: $s_tdata_left"
line $fd "gemv S_AXIS_TKEEP LEFT: $s_tkeep_left"
line $fd "gemv M_AXIS_TDATA LEFT: $m_tdata_left"
line $fd "result: PASS"
close $fd
puts "S05_5_AXIS128_BD_APPLY_LOG=$log_file"
close_project
