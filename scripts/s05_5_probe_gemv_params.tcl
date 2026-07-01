set repo_root [file normalize [file join [file dirname [info script]] ..]]
set project_xpr [file join $repo_root hw vivado_project GPTalk.xpr]
set bd_file [file join $repo_root hw vivado_project GPTalk.srcs sources_1 bd design_1 design_1.bd]
set log_file [file join $repo_root logs s05_5_gemv_param_probe.txt]
file mkdir [file dirname $log_file]
open_project $project_xpr
open_bd_design $bd_file
set cell [get_bd_cells -quiet gemv_q8_0_dma_top_0]
if {![llength $cell]} {
    error "missing gemv_q8_0_dma_top_0"
}
if {[catch {update_module_reference -force $cell} update_msg]} {
    catch {update_module_reference $cell} update_msg
}
set fd [open $log_file w]
puts $fd "S05.5 GEMV module_ref parameter probe"
puts $fd "update_module_reference: $update_msg"
puts $fd ""
foreach prop [lsort [list_property $cell]] {
    if {[regexp -nocase {(AXIS|TDATA|TKEEP|LANES|PARAM|CONFIG|VLNV)} $prop]} {
        puts $fd "$prop: [get_property $prop $cell]"
    }
}
close $fd
puts "S05_5_GEMV_PARAM_PROBE_LOG=$log_file"
close_project
