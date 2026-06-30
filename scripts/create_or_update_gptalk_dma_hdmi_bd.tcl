# BLOCKED: this legacy script recreates GPTalk design_1 with HDMI TX plus GEMV DMA.
#
# This script intentionally keeps the existing GPTalk.xpr as the active project.
# It is currently unsafe because it replaces design_1 with:
#   - a newly generated Zynq PS UART/USB/SD/GEM board preset
#   - HDMI TX framebuffer path: AXI VDMA MM2S -> AXIS -> Video Out -> rgb2dvi
#   - GEMV DMA path isolated on a timing-safe configurable PL clock
#
# It must not be used again until rewritten to preserve the existing GPTalk PS7
# DDR/MIO/SD/UART settings and only add/update PL peripherals.
#
# Run:
#   vivado -mode batch -source scripts/create_or_update_gptalk_dma_hdmi_bd.tcl

set script_dir [file normalize [file dirname [info script]]]
set repo_root [file normalize [file join $script_dir ..]]
set project_xpr [file join $repo_root hw vivado_project GPTalk.xpr]
set project_dir [file dirname $project_xpr]
set log_dir [file join $repo_root logs]
file mkdir $log_dir

if {![file exists $project_xpr]} {
    error "Active Vivado project is missing: $project_xpr"
}

error "BLOCKED: scripts/create_or_update_gptalk_dma_hdmi_bd.tcl deletes design_1 and recreates processing_system7_0. Rewrite it as a PS7-preserving updater before using it."

set board_repo [file join $repo_root vivado_board_files digilent-vivado-boards new board_files]
set digilent_ip_repo [file join $repo_root third_party digilent-vivado-library ip]
set digilent_if_repo [file join $repo_root third_party digilent-vivado-library if]
set board_part "digilentinc.com:zybo-z7-20:part0:1.2"

set bd_name "design_1"
proc env_default {name default_value} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $default_value
}

set fclk0_mhz [expr {double([env_default GPTALK_CTRL_CLK_MHZ 100.000000])}]
set fclk1_mhz 133.333333
set gemv_clk_mhz [expr {double([env_default GPTALK_PL_CLK_MHZ $fclk0_mhz])}]
set gemv_freq_hz_raw [env_default GPTALK_PL_ACTUAL_FREQ_HZ [expr {int(round($gemv_clk_mhz * 1000000.0))}]]
if {![string is integer -strict $gemv_freq_hz_raw] || $gemv_freq_hz_raw <= 0} {
    error "GPTALK_PL_ACTUAL_FREQ_HZ must be a positive integer, got: $gemv_freq_hz_raw"
}
set gemv_freq_hz $gemv_freq_hz_raw

set vdma_base   0x43010000
set vtc_base    0x43C10000
set dynclk_base 0x43C20000
set gemv_base   0x43CA0000
set dma_base    0x40400000
set bram_base   0x42000000

set rtl_core [file join $repo_root vivado_ip rtl gemv_q8_0_stream_core.v]
set rtl_ctrl [file join $repo_root vivado_ip rtl gemv_q8_0_ctrl_axi_lite.v]
set rtl_top  [file join $repo_root vivado_ip rtl gemv_q8_0_dma_top.v]
foreach required [list $rtl_core $rtl_ctrl $rtl_top $digilent_ip_repo $digilent_if_repo] {
    if {![file exists $required]} {
        error "missing required file or directory: $required"
    }
}

proc try_set_properties {obj prop_values} {
    foreach {prop value} $prop_values {
        if {[catch {set_property $prop $value $obj} msg]} {
            puts "WARN: could not set $prop on $obj: $msg"
        }
    }
}

proc connect_pin_list_to_net {net_pin pins} {
    foreach pin $pins {
        set p [get_bd_pins -quiet $pin]
        if {[llength $p]} {
            connect_bd_net $net_pin $p
        } else {
            puts "WARN: missing pin for clock/reset net: $pin"
        }
    }
}

proc set_addr_segment {pattern offset range} {
    set segs [get_bd_addr_segs -quiet -regexp $pattern]
    if {[llength $segs] == 0} {
        puts "WARN: no address segment matched $pattern"
        return
    }
    foreach seg $segs {
        set_property offset $offset $seg
        set_property range $range $seg
    }
}

open_project $project_xpr
set_property target_language Verilog [current_project]
if {[file exists $board_repo]} {
    set_param board.repoPaths [list $board_repo]
}
if {[llength [get_board_parts -quiet $board_part]]} {
    set_property board_part $board_part [current_project]
}
set_property ip_repo_paths [list $digilent_if_repo $digilent_ip_repo] [current_project]
update_ip_catalog

foreach rtl [list $rtl_core $rtl_ctrl $rtl_top] {
    if {[llength [get_files -quiet $rtl]] == 0} {
        add_files -norecurse -fileset sources_1 $rtl
    }
    set_property file_type Verilog [get_files $rtl]
}
update_compile_order -fileset sources_1

set old_bd_files [get_files -quiet -regexp ".*/bd/${bd_name}/${bd_name}\\.bd$"]
if {[llength $old_bd_files] > 0} {
    remove_files $old_bd_files
}
file delete -force [file join $project_dir GPTalk.srcs sources_1 bd $bd_name]
file delete -force [file join $project_dir GPTalk.gen sources_1 bd $bd_name]

create_bd_design $bd_name

create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7 processing_system7_0
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} \
    [get_bd_cells processing_system7_0]
set_property -dict [list \
    CONFIG.PCW_USE_M_AXI_GP0 {1} \
    CONFIG.PCW_USE_S_AXI_HP0 {1} \
    CONFIG.PCW_USE_S_AXI_HP1 {1} \
    CONFIG.PCW_USE_FABRIC_INTERRUPT {1} \
    CONFIG.PCW_IRQ_F2P_INTR {1} \
    CONFIG.PCW_EN_CLK0_PORT {1} \
    CONFIG.PCW_EN_CLK1_PORT {1} \
    CONFIG.PCW_EN_CLK2_PORT {1} \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ $fclk0_mhz \
    CONFIG.PCW_FPGA1_PERIPHERAL_FREQMHZ $fclk1_mhz \
    CONFIG.PCW_FPGA2_PERIPHERAL_FREQMHZ $gemv_clk_mhz \
    CONFIG.PCW_EN_I2C0 {1} \
    CONFIG.PCW_EN_EMIO_I2C0 {1} \
    CONFIG.PCW_I2C0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_I2C0_I2C0_IO {EMIO} \
    CONFIG.PCW_I2C0_GRP_INT_ENABLE {1} \
    CONFIG.PCW_I2C0_GRP_INT_IO {EMIO} \
] [get_bd_cells processing_system7_0]

create_bd_intf_port -mode Master -vlnv digilentinc.com:interface:tmds_rtl:1.0 hdmi_out
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 hdmi_out_ddc

create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset rst_ps7_0_100M
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset rst_ps7_0_133M
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset rst_ps7_0_gemv

create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect axi_ctrl_smc
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {6}] [get_bd_cells axi_ctrl_smc]
create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect axi_video_hp0_smc
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] [get_bd_cells axi_video_hp0_smc]
create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect axi_gemv_hp1_smc
set_property -dict [list CONFIG.NUM_SI {2} CONFIG.NUM_MI {1}] [get_bd_cells axi_gemv_hp1_smc]

create_bd_cell -type ip -vlnv xilinx.com:ip:axi_clock_converter axi_gemv_ctrl_clkconv
set_property -dict [list CONFIG.PROTOCOL {AXI4LITE} CONFIG.ADDR_WIDTH {12} CONFIG.DATA_WIDTH {32}] [get_bd_cells axi_gemv_ctrl_clkconv]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_clock_converter axi_bram_ctrl_clkconv
set_property -dict [list CONFIG.PROTOCOL {AXI4} CONFIG.ADDR_WIDTH {16} CONFIG.DATA_WIDTH {32}] [get_bd_cells axi_bram_ctrl_clkconv]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_clock_converter axi_dma_ctrl_clkconv
set_property -dict [list CONFIG.PROTOCOL {AXI4LITE} CONFIG.ADDR_WIDTH {10} CONFIG.DATA_WIDTH {32}] [get_bd_cells axi_dma_ctrl_clkconv]

create_bd_cell -type ip -vlnv digilentinc.com:ip:axi_dynclk axi_dynclk_0
create_bd_cell -type ip -vlnv xilinx.com:ip:v_tc v_tc_out
try_set_properties [get_bd_cells v_tc_out] [list CONFIG.enable_detection {false}]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma axi_vdma_1
try_set_properties [get_bd_cells axi_vdma_1] [list \
    CONFIG.c_include_mm2s_dre {1} \
    CONFIG.c_include_s2mm {0} \
    CONFIG.c_m_axis_mm2s_tdata_width {24} \
    CONFIG.c_mm2s_linebuffer_depth {2048} \
    CONFIG.c_mm2s_max_burst_length {32} \
    CONFIG.c_num_fstores {1} \
    CONFIG.c_s2mm_genlock_mode {0} \
]
create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter axis_subset_converter_out
try_set_properties [get_bd_cells axis_subset_converter_out] [list \
    CONFIG.M_HAS_TLAST {1} \
    CONFIG.M_TDATA_NUM_BYTES {3} \
    CONFIG.M_TUSER_WIDTH {1} \
    CONFIG.S_HAS_TLAST {1} \
    CONFIG.S_TDATA_NUM_BYTES {3} \
    CONFIG.S_TUSER_WIDTH {1} \
    CONFIG.TDATA_REMAP {tdata[23:16],tdata[7:0],tdata[15:8]} \
    CONFIG.TKEEP_REMAP {1'b0} \
    CONFIG.TLAST_REMAP {tlast[0]} \
    CONFIG.TUSER_REMAP {tuser[0:0]} \
]
create_bd_cell -type ip -vlnv xilinx.com:ip:v_axi4s_vid_out v_axi4s_vid_out_0
try_set_properties [get_bd_cells v_axi4s_vid_out_0] [list \
    CONFIG.C_ADDR_WIDTH {12} \
    CONFIG.C_HAS_ASYNC_CLK {1} \
    CONFIG.C_VTG_MASTER_SLAVE {1} \
]
create_bd_cell -type ip -vlnv digilentinc.com:ip:rgb2dvi rgb2dvi_1
try_set_properties [get_bd_cells rgb2dvi_1] [list \
    CONFIG.TMDS_BOARD_INTERFACE {hdmi_out} \
    CONFIG.kGenerateSerialClk {false} \
    CONFIG.kRstActiveHigh {false} \
]

create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma axi_dma_0
try_set_properties [get_bd_cells axi_dma_0] [list \
    CONFIG.c_include_sg {0} \
    CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_s2mm {1} \
    CONFIG.c_m_axi_mm2s_data_width {32} \
    CONFIG.c_m_axi_s2mm_data_width {32} \
    CONFIG.c_m_axis_mm2s_tdata_width {32} \
    CONFIG.c_s_axis_s2mm_tdata_width {32} \
    CONFIG.c_include_mm2s_dre {1} \
    CONFIG.c_include_s2mm_dre {1} \
]
create_bd_cell -type ip -vlnv xilinx.com:ip:axis_data_fifo mm2s_axis_fifo
try_set_properties [get_bd_cells mm2s_axis_fifo] [list CONFIG.TDATA_NUM_BYTES {4} CONFIG.FIFO_DEPTH {1024} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1}]
create_bd_cell -type ip -vlnv xilinx.com:ip:axis_data_fifo s2mm_axis_fifo
try_set_properties [get_bd_cells s2mm_axis_fifo] [list CONFIG.TDATA_NUM_BYTES {4} CONFIG.FIFO_DEPTH {1024} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1}]
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_bram_ctrl axi_input_bram_ctrl
try_set_properties [get_bd_cells axi_input_bram_ctrl] [list CONFIG.DATA_WIDTH {32} CONFIG.SINGLE_PORT_BRAM {1}]
create_bd_cell -type ip -vlnv xilinx.com:ip:blk_mem_gen input_vector_bram
try_set_properties [get_bd_cells input_vector_bram] [list \
    CONFIG.Memory_Type {True_Dual_Port_RAM} \
    CONFIG.Use_Byte_Write_Enable {true} \
    CONFIG.Byte_Size {8} \
    CONFIG.Write_Width_A {32} \
    CONFIG.Read_Width_A {32} \
    CONFIG.Write_Width_B {32} \
    CONFIG.Read_Width_B {32} \
    CONFIG.Write_Depth_A {16384} \
    CONFIG.Register_PortA_Output_of_Memory_Primitives {false} \
    CONFIG.Register_PortB_Output_of_Memory_Primitives {false} \
]
create_bd_cell -type module -reference gemv_q8_0_dma_top gemv_q8_0_dma_top_0

create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat xlconcat_0
set_property -dict [list CONFIG.NUM_PORTS {4}] [get_bd_cells xlconcat_0]

set fclk0 [get_bd_pins processing_system7_0/FCLK_CLK0]
set fclk1 [get_bd_pins processing_system7_0/FCLK_CLK1]
set fclk2 [get_bd_pins processing_system7_0/FCLK_CLK2]
set rst0n [get_bd_pins rst_ps7_0_100M/peripheral_aresetn]
set rst1n [get_bd_pins rst_ps7_0_133M/peripheral_aresetn]
set rst2n [get_bd_pins rst_ps7_0_gemv/peripheral_aresetn]

connect_bd_net $fclk0 [get_bd_pins rst_ps7_0_100M/slowest_sync_clk]
connect_bd_net $fclk1 [get_bd_pins rst_ps7_0_133M/slowest_sync_clk]
connect_bd_net $fclk2 [get_bd_pins rst_ps7_0_gemv/slowest_sync_clk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] \
    [get_bd_pins rst_ps7_0_100M/ext_reset_in] \
    [get_bd_pins rst_ps7_0_133M/ext_reset_in] \
    [get_bd_pins rst_ps7_0_gemv/ext_reset_in]

connect_pin_list_to_net $fclk0 [list \
    processing_system7_0/M_AXI_GP0_ACLK \
    axi_ctrl_smc/aclk \
    axi_dynclk_0/REF_CLK_I \
    axi_dynclk_0/s_axi_lite_aclk \
    v_tc_out/s_axi_aclk \
    axi_vdma_1/s_axi_lite_aclk \
    axi_dma_ctrl_clkconv/s_axi_aclk \
    axi_gemv_ctrl_clkconv/s_axi_aclk \
    axi_bram_ctrl_clkconv/s_axi_aclk \
]
connect_pin_list_to_net $fclk1 [list \
    processing_system7_0/S_AXI_HP0_ACLK \
    axi_video_hp0_smc/aclk \
    axi_vdma_1/m_axi_mm2s_aclk \
    axi_vdma_1/m_axis_mm2s_aclk \
    axis_subset_converter_out/aclk \
    v_axi4s_vid_out_0/aclk \
]
connect_pin_list_to_net $fclk2 [list \
    processing_system7_0/S_AXI_HP1_ACLK \
    axi_gemv_hp1_smc/aclk \
    axi_dma_0/m_axi_mm2s_aclk \
    axi_dma_0/m_axi_s2mm_aclk \
    axi_dma_0/s_axi_lite_aclk \
    mm2s_axis_fifo/s_axis_aclk \
    s2mm_axis_fifo/s_axis_aclk \
    axi_input_bram_ctrl/s_axi_aclk \
    gemv_q8_0_dma_top_0/S_AXI_ACLK \
    axi_dma_ctrl_clkconv/m_axi_aclk \
    axi_gemv_ctrl_clkconv/m_axi_aclk \
    axi_bram_ctrl_clkconv/m_axi_aclk \
]

connect_pin_list_to_net $rst0n [list \
    axi_ctrl_smc/aresetn \
    axi_dynclk_0/s_axi_lite_aresetn \
    v_tc_out/s_axi_aresetn \
    axi_vdma_1/axi_resetn \
    axi_dma_ctrl_clkconv/s_axi_aresetn \
    axi_gemv_ctrl_clkconv/s_axi_aresetn \
    axi_bram_ctrl_clkconv/s_axi_aresetn \
]
connect_pin_list_to_net $rst1n [list \
    axi_video_hp0_smc/aresetn \
    axis_subset_converter_out/aresetn \
    v_axi4s_vid_out_0/aresetn \
]
connect_pin_list_to_net $rst2n [list \
    axi_gemv_hp1_smc/aresetn \
    axi_dma_0/axi_resetn \
    mm2s_axis_fifo/s_axis_aresetn \
    s2mm_axis_fifo/s_axis_aresetn \
    axi_input_bram_ctrl/s_axi_aresetn \
    gemv_q8_0_dma_top_0/S_AXI_ARESETN \
    axi_dma_ctrl_clkconv/m_axi_aresetn \
    axi_gemv_ctrl_clkconv/m_axi_aresetn \
    axi_bram_ctrl_clkconv/m_axi_aresetn \
]

connect_bd_intf_net [get_bd_intf_pins processing_system7_0/IIC_0] [get_bd_intf_ports hdmi_out_ddc]
connect_bd_intf_net [get_bd_intf_pins processing_system7_0/M_AXI_GP0] [get_bd_intf_pins axi_ctrl_smc/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_smc/M00_AXI] [get_bd_intf_pins axi_vdma_1/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_smc/M01_AXI] [get_bd_intf_pins v_tc_out/ctrl]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_smc/M02_AXI] [get_bd_intf_pins axi_dynclk_0/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_smc/M03_AXI] [get_bd_intf_pins axi_dma_ctrl_clkconv/S_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_ctrl_clkconv/M_AXI] [get_bd_intf_pins axi_dma_0/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_smc/M04_AXI] [get_bd_intf_pins axi_gemv_ctrl_clkconv/S_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_gemv_ctrl_clkconv/M_AXI] [get_bd_intf_pins gemv_q8_0_dma_top_0/S_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ctrl_smc/M05_AXI] [get_bd_intf_pins axi_bram_ctrl_clkconv/S_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_bram_ctrl_clkconv/M_AXI] [get_bd_intf_pins axi_input_bram_ctrl/S_AXI]

connect_bd_intf_net [get_bd_intf_pins axi_vdma_1/M_AXI_MM2S] [get_bd_intf_pins axi_video_hp0_smc/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_video_hp0_smc/M00_AXI] [get_bd_intf_pins processing_system7_0/S_AXI_HP0]
connect_bd_intf_net [get_bd_intf_pins axi_vdma_1/M_AXIS_MM2S] [get_bd_intf_pins axis_subset_converter_out/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins axis_subset_converter_out/M_AXIS] [get_bd_intf_pins v_axi4s_vid_out_0/video_in]
connect_bd_intf_net [get_bd_intf_pins v_axi4s_vid_out_0/vid_io_out] [get_bd_intf_pins rgb2dvi_1/RGB]
connect_bd_intf_net [get_bd_intf_pins v_axi4s_vid_out_0/vtiming_in] [get_bd_intf_pins v_tc_out/vtiming_out]
connect_bd_intf_net [get_bd_intf_pins rgb2dvi_1/TMDS] [get_bd_intf_ports hdmi_out]
connect_bd_net [get_bd_pins axi_dynclk_0/PXL_CLK_O] \
    [get_bd_pins rgb2dvi_1/PixelClk] \
    [get_bd_pins v_axi4s_vid_out_0/vid_io_out_clk] \
    [get_bd_pins v_tc_out/clk]
connect_bd_net [get_bd_pins axi_dynclk_0/PXL_CLK_5X_O] [get_bd_pins rgb2dvi_1/SerialClk]
connect_bd_net [get_bd_pins axi_dynclk_0/LOCKED_O] [get_bd_pins rgb2dvi_1/aRst_n]
connect_bd_net [get_bd_pins v_axi4s_vid_out_0/vtg_ce] [get_bd_pins v_tc_out/gen_clken]

connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXI_MM2S] [get_bd_intf_pins axi_gemv_hp1_smc/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXI_S2MM] [get_bd_intf_pins axi_gemv_hp1_smc/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_gemv_hp1_smc/M00_AXI] [get_bd_intf_pins processing_system7_0/S_AXI_HP1]
connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXIS_MM2S] [get_bd_intf_pins mm2s_axis_fifo/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins mm2s_axis_fifo/M_AXIS] [get_bd_intf_pins gemv_q8_0_dma_top_0/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins gemv_q8_0_dma_top_0/M_AXIS] [get_bd_intf_pins s2mm_axis_fifo/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins s2mm_axis_fifo/M_AXIS] [get_bd_intf_pins axi_dma_0/S_AXIS_S2MM]
connect_bd_intf_net [get_bd_intf_pins axi_input_bram_ctrl/BRAM_PORTA] [get_bd_intf_pins input_vector_bram/BRAM_PORTA]
connect_bd_net [get_bd_pins gemv_q8_0_dma_top_0/INPUT_BRAM_CLK]  [get_bd_pins input_vector_bram/clkb]
connect_bd_net [get_bd_pins gemv_q8_0_dma_top_0/INPUT_BRAM_RST]  [get_bd_pins input_vector_bram/rstb]
connect_bd_net [get_bd_pins gemv_q8_0_dma_top_0/INPUT_BRAM_EN]   [get_bd_pins input_vector_bram/enb]
connect_bd_net [get_bd_pins gemv_q8_0_dma_top_0/INPUT_BRAM_WE]   [get_bd_pins input_vector_bram/web]
connect_bd_net [get_bd_pins gemv_q8_0_dma_top_0/INPUT_BRAM_ADDR] [get_bd_pins input_vector_bram/addrb]
connect_bd_net [get_bd_pins gemv_q8_0_dma_top_0/INPUT_BRAM_DIN]  [get_bd_pins input_vector_bram/dinb]
connect_bd_net [get_bd_pins gemv_q8_0_dma_top_0/INPUT_BRAM_DOUT] [get_bd_pins input_vector_bram/doutb]

connect_bd_net [get_bd_pins axi_vdma_1/mm2s_introut] [get_bd_pins xlconcat_0/In0]
connect_bd_net [get_bd_pins v_tc_out/irq] [get_bd_pins xlconcat_0/In1]
connect_bd_net [get_bd_pins axi_dma_0/mm2s_introut] [get_bd_pins xlconcat_0/In2]
connect_bd_net [get_bd_pins axi_dma_0/s2mm_introut] [get_bd_pins xlconcat_0/In3]
connect_bd_net [get_bd_pins xlconcat_0/dout] [get_bd_pins processing_system7_0/IRQ_F2P]

assign_bd_address
set_addr_segment {.*SEG_axi_vdma_1.*Reg.*} $vdma_base 64K
set_addr_segment {.*SEG_v_tc_out.*Reg.*|.*SEG_v_tc_out.*ctrl.*} $vtc_base 64K
set_addr_segment {.*SEG_axi_dynclk_0.*reg.*|.*SEG_axi_dynclk_0.*Reg.*} $dynclk_base 64K
set_addr_segment {.*SEG_axi_dma_0.*Reg.*} $dma_base 64K
set_addr_segment {.*SEG_gemv_q8_0_dma_top_0.*} $gemv_base 4K
set_addr_segment {.*SEG_axi_input_bram_ctrl.*} $bram_base 64K

validate_bd_design
save_bd_design

set address_report [file join $log_dir hw_dma_hdmi_address_map.txt]
set addr_fd [open $address_report w]
puts $addr_fd "GPTalk HDMI + GEMV DMA address map"
puts $addr_fd "Generated by scripts/create_or_update_gptalk_dma_hdmi_bd.tcl"
puts $addr_fd ""
puts $addr_fd "Active Vivado project: $project_xpr"
puts $addr_fd [format "FCLK0/control+dynclk ref: %.3f MHz" $fclk0_mhz]
puts $addr_fd [format "FCLK1/video HP+AXIS: %.3f MHz" $fclk1_mhz]
puts $addr_fd [format "FCLK2/GEMV domain: %.6f MHz (%d Hz)" $gemv_clk_mhz $gemv_freq_hz]
puts $addr_fd [format "AXI VDMA display base: 0x%08X" $vdma_base]
puts $addr_fd [format "VTC output base:       0x%08X" $vtc_base]
puts $addr_fd [format "AXI dynclk base:       0x%08X" $dynclk_base]
puts $addr_fd [format "GEMV control base:     0x%08X" $gemv_base]
puts $addr_fd [format "AXI DMA base:          0x%08X" $dma_base]
puts $addr_fd [format "Input BRAM base:       0x%08X" $bram_base]
puts $addr_fd ""
foreach seg [get_bd_addr_segs -quiet -regexp {.*SEG_.*}] {
    puts $addr_fd [format "%s OFFSET=%s RANGE=%s" $seg [get_property OFFSET $seg] [get_property RANGE $seg]]
}
close $addr_fd

set bd_file [get_files [file join $project_dir GPTalk.srcs sources_1 bd $bd_name ${bd_name}.bd]]
make_wrapper -files $bd_file -top
set wrapper_file [file join $project_dir GPTalk.gen sources_1 bd $bd_name hdl ${bd_name}_wrapper.v]
if {[file exists $wrapper_file] && [llength [get_files -quiet $wrapper_file]] == 0} {
    add_files -norecurse $wrapper_file
}
set_property top ${bd_name}_wrapper [current_fileset]
update_compile_order -fileset sources_1
close_project

puts "S015_GPTALK_DMA_HDMI_BD_CREATED=1"
puts "ACTIVE_PROJECT=$project_xpr"
puts "ADDRESS_REPORT=$address_report"
puts [format "GEMV_CONTROL_BASE=0x%08X" $gemv_base]
puts [format "AXI_DMA_BASE=0x%08X" $dma_base]
puts [format "INPUT_BRAM_BASE=0x%08X" $bram_base]
puts [format "HDMI_VDMA_BASE=0x%08X" $vdma_base]
puts [format "HDMI_VTC_BASE=0x%08X" $vtc_base]
puts [format "HDMI_DYNCLK_BASE=0x%08X" $dynclk_base]
