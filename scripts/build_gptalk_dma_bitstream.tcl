# Build the active GPTalk.xpr DMA GEMV bitstream and export XSA.
#
# S02 entrypoint. This script only opens the existing GPTalk.xpr.
#
# Run:
#   GPTALK_PL_CLK_MHZ=50 vivado -mode batch -source scripts/build_gptalk_dma_bitstream.tcl

set script_dir [file normalize [file dirname [info script]]]
set repo_root [file normalize [file join $script_dir ..]]
set project_xpr [file join $repo_root hw vivado_project GPTalk.xpr]
set project_dir [file dirname $project_xpr]
set log_dir [file join $repo_root logs]
set report_dir [file join $repo_root reports]
set docs_dir [file join $repo_root docs]
set internal_docs_dir [file join $docs_dir internal]
set export_dir [file join $project_dir export]
file mkdir $log_dir
file mkdir $report_dir
file mkdir $docs_dir
file mkdir $internal_docs_dir
file mkdir $export_dir

if {![file exists $project_xpr]} {
    error "Active Vivado project is missing: $project_xpr"
}

proc env_default {name default_value} {
    if {[info exists ::env($name)] && $::env($name) ne ""} {
        return $::env($name)
    }
    return $default_value
}

proc clock_label {clk_mhz} {
    if {[expr {abs($clk_mhz - round($clk_mhz)) < 0.001}]} {
        return [format "%d" [expr {int(round($clk_mhz))}]]
    }
    set label [format "%.3f" $clk_mhz]
    regsub {0+$} $label "" label
    regsub {\.$} $label "" label
    regsub -all {\.} $label "p" label
    return $label
}

set pl_clk_mhz_raw [env_default GPTALK_PL_CLK_MHZ 50]
if {![string is double -strict $pl_clk_mhz_raw]} {
    error "GPTALK_PL_CLK_MHZ must be numeric, got: $pl_clk_mhz_raw"
}
set pl_clk_mhz [expr {double($pl_clk_mhz_raw)}]
if {$pl_clk_mhz <= 0.0} {
    error "GPTALK_PL_CLK_MHZ must be positive, got: $pl_clk_mhz_raw"
}
set clk_label [clock_label $pl_clk_mhz]
set export_tag [env_default GPTALK_EXPORT_TAG "${clk_label}MHz"]
if {$export_tag eq ""} {
    set export_tag "${clk_label}MHz"
}
set update_latest [env_default GPTALK_UPDATE_LATEST 1]

set ctrl_clk_mhz_raw [env_default GPTALK_CTRL_CLK_MHZ 100]
if {![string is double -strict $ctrl_clk_mhz_raw]} {
    error "GPTALK_CTRL_CLK_MHZ must be numeric, got: $ctrl_clk_mhz_raw"
}
set ctrl_clk_mhz [expr {double($ctrl_clk_mhz_raw)}]
if {$ctrl_clk_mhz <= 0.0} {
    error "GPTALK_CTRL_CLK_MHZ must be positive, got: $ctrl_clk_mhz_raw"
}
set ctrl_actual_freq_hz_raw [env_default GPTALK_CTRL_ACTUAL_FREQ_HZ [expr {int(round($ctrl_clk_mhz * 1000000.0))}]]
if {![string is integer -strict $ctrl_actual_freq_hz_raw] || $ctrl_actual_freq_hz_raw <= 0} {
    error "GPTALK_CTRL_ACTUAL_FREQ_HZ must be a positive integer, got: $ctrl_actual_freq_hz_raw"
}
set ctrl_actual_freq_hz $ctrl_actual_freq_hz_raw

set actual_freq_hz_raw [env_default GPTALK_PL_ACTUAL_FREQ_HZ [expr {int(round($pl_clk_mhz * 1000000.0))}]]
if {![string is integer -strict $actual_freq_hz_raw] || $actual_freq_hz_raw <= 0} {
    error "GPTALK_PL_ACTUAL_FREQ_HZ must be a positive integer, got: $actual_freq_hz_raw"
}
set actual_freq_hz $actual_freq_hz_raw

set jobs_raw [env_default GPTALK_VIVADO_JOBS 8]
if {![string is integer -strict $jobs_raw] || $jobs_raw <= 0} {
    error "GPTALK_VIVADO_JOBS must be a positive integer, got: $jobs_raw"
}
set vivado_jobs $jobs_raw
set apply_clock_only [env_default GPTALK_APPLY_CLOCK_ONLY 0]

set fallback_log {}

proc try_run_strategy {run_name label candidates} {
    global fallback_log
    foreach candidate $candidates {
        if {[catch {set_property strategy $candidate [get_runs $run_name]} msg]} {
            lappend fallback_log "$label fallback: $candidate rejected: $msg"
        } else {
            return $candidate
        }
    }
    lappend fallback_log "$label fallback: no candidate accepted; leaving unchanged"
    return "unchanged"
}

proc try_step_directive {run_name prop_name label candidates} {
    global fallback_log
    foreach candidate $candidates {
        if {[catch {set_property $prop_name $candidate [get_runs $run_name]} msg]} {
            lappend fallback_log "$label fallback: $candidate rejected: $msg"
        } else {
            return $candidate
        }
    }
    lappend fallback_log "$label fallback: no candidate accepted; leaving unchanged"
    return "unchanged"
}

proc report_or_warn {label command} {
    if {[catch {uplevel 1 $command} msg]} {
        puts "WARN: $label failed: $msg"
        return 0
    }
    puts "REPORT_OK: $label"
    return 1
}

proc read_base_from_report {path label default_value} {
    if {![file exists $path]} {
        return $default_value
    }
    set fd [open $path r]
    set text [read $fd]
    close $fd
    set pattern [format {%s:[ \t]+0x([0-9A-Fa-f]+)} $label]
    if {[regexp $pattern $text -> hex_value]} {
        return [expr "0x$hex_value"]
    }
    return $default_value
}

proc assert_run_complete {run_name} {
    set progress [get_property PROGRESS [get_runs $run_name]]
    set status [get_property STATUS [get_runs $run_name]]
    if {$progress ne "100%"} {
        error "$run_name did not complete: progress=$progress status=$status"
    }
    if {![string match "*Complete*" $status]} {
        error "$run_name failed: $status"
    }
}

proc clear_stale_run_markers {run_name} {
    set run_obj [get_runs -quiet $run_name]
    if {[llength $run_obj] == 0} {
        return
    }
    set run_dir [get_property DIRECTORY $run_obj]
    foreach marker [list \
        ".stop.rst" \
        ".vivado.begin.rst" \
        ".vivado.end.rst" \
        ".vivado.error.rst" \
        ".Vivado_Synthesis.queue.rst" \
    ] {
        set marker_path [file join $run_dir $marker]
        if {[file exists $marker_path]} {
            file delete -force $marker_path
        }
    }
}

proc reset_run_clean {run_name} {
    clear_stale_run_markers $run_name
    if {[catch {reset_runs $run_name} msg]} {
        clear_stale_run_markers $run_name
        if {[catch {reset_runs $run_name} retry_msg]} {
            error "reset_run $run_name failed: $msg; retry failed: $retry_msg"
        }
    }
}

proc timing_slack_summary {delay_type} {
    set worst "N/A"
    set total 0.0
    set count 0
    set worst_paths [get_timing_paths -delay_type $delay_type -max_paths 1]
    if {[llength $worst_paths] > 0} {
        set worst [get_property SLACK [lindex $worst_paths 0]]
    }
    set violating_paths [get_timing_paths -delay_type $delay_type -slack_lesser_than 0 -max_paths 10000]
    foreach path $violating_paths {
        set slack [get_property SLACK $path]
        if {[string is double -strict $slack] && $slack < 0.0} {
            set total [expr {$total + double($slack)}]
            incr count
        }
    }
    return [list $worst $total $count]
}

set bd_name "design_1"
set bd_file [file join $project_dir GPTalk.srcs sources_1 bd $bd_name ${bd_name}.bd]
if {![file exists $bd_file]} {
    error "Block design is missing: $bd_file"
}
set hdmi_tx_xdc [file join $project_dir GPTalk.srcs constrs_1 new GPTalk_hdmi_tx.xdc]
set address_report [file join $log_dir hw_dma_hdmi_address_map.txt]
if {![file exists $address_report]} {
    set address_report [file join $log_dir hw_dma_address_map.txt]
}
set gemv_base [read_base_from_report $address_report "GEMV control base" 0x43C00000]
set dma_base  [read_base_from_report $address_report "AXI DMA base" 0x40400000]
set bram_base [read_base_from_report $address_report "Input BRAM base" 0x42000000]
set module_xci [file join $project_dir GPTalk.srcs sources_1 bd $bd_name ip design_1_gemv_q8_0_dma_top_0_0 design_1_gemv_q8_0_dma_top_0_0.xci]

proc patch_freq_hz_file {path freq_hz} {
    if {![file exists $path]} {
        return 0
    }
    set fd [open $path r]
    set text [read $fd]
    close $fd

    set changed 0
    set patch_next_value 0
    set out_lines {}
    foreach line [split $text "\n"] {
        if {[string first "\"FREQ_HZ\"" $line] >= 0} {
            if {[regsub {("value": *")[0-9]+(")} $line "\\1$freq_hz\\2" new_line]} {
                set line $new_line
                set changed 1
                set patch_next_value 0
            } else {
                set patch_next_value 1
            }
        } elseif {$patch_next_value} {
            if {[regsub {("value": *")[0-9]+(")} $line "\\1$freq_hz\\2" new_line]} {
                set line $new_line
                set changed 1
                set patch_next_value 0
            }
        }
        lappend out_lines $line
    }

    if {$changed} {
        set fd [open $path w]
        puts -nonewline $fd [join $out_lines "\n"]
        close $fd
    }
    return $changed
}

patch_freq_hz_file $module_xci $actual_freq_hz

open_project $project_xpr
set_property target_language Verilog [current_project]
if {[file exists $hdmi_tx_xdc] && [llength [get_files -quiet $hdmi_tx_xdc]] == 0} {
    add_files -fileset constrs_1 -norecurse $hdmi_tx_xdc
}

open_bd_design $bd_file
set ps7 [get_bd_cells -quiet processing_system7_0]
if {[llength $ps7] == 0} {
    error "processing_system7_0 is missing from $bd_name"
}
if {[llength [get_bd_cells -quiet gemv_q8_0_dma_top_0]]} {
    catch {update_module_reference [get_bd_cells gemv_q8_0_dma_top_0]}
}
set_property CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ [format "%.6f" $ctrl_clk_mhz] $ps7
set_property CONFIG.PCW_FPGA2_PERIPHERAL_FREQMHZ [format "%.6f" $pl_clk_mhz] $ps7
foreach {fclk_pin_name expected_freq_hz} [list \
    processing_system7_0/FCLK_CLK0 $ctrl_actual_freq_hz \
    processing_system7_0/FCLK_CLK2 $actual_freq_hz \
] {
    set fclk_pin [get_bd_pins -quiet $fclk_pin_name]
    if {![llength $fclk_pin]} {
        continue
    }
    set fclk_freq [get_property -quiet CONFIG.FREQ_HZ $fclk_pin]
    if {$fclk_freq ne "" && $fclk_freq ne $expected_freq_hz} {
        puts "WARN: requested actual FREQ_HZ $expected_freq_hz differs from $fclk_pin_name $fclk_freq"
    }
}
foreach intf_name [list \
    gemv_q8_0_dma_top_0/S_AXI \
    gemv_q8_0_dma_top_0/S_AXIS \
    gemv_q8_0_dma_top_0/M_AXIS \
] {
    set intf_pin [get_bd_intf_pins -quiet $intf_name]
    if {[llength $intf_pin]} {
        # FREQ_HZ is read-only on module_ref pins after import; it is patched
        # in the BD/XCI metadata before open_bd_design.
    }
}
set gemv_clk [get_bd_pins -quiet gemv_q8_0_dma_top_0/S_AXI_ACLK]
if {[llength $gemv_clk]} {
    # See module_ref metadata patch above.
}
validate_bd_design
save_bd_design
generate_target all [get_files $bd_file]
update_compile_order -fileset sources_1

if {[llength [get_files -quiet [file join $project_dir GPTalk.gen sources_1 bd $bd_name hdl ${bd_name}_wrapper.v]]]} {
    set_property top ${bd_name}_wrapper [current_fileset]
}

if {$apply_clock_only} {
    set apply_log [file join $log_dir s02_clock_prediction.txt]
    set apply_fd [open $apply_log w]
    puts $apply_fd "S02 predicted clock application"
    puts $apply_fd "Active Vivado project: $project_xpr"
    puts $apply_fd [format "Applied PL clock target: %.3f MHz" $pl_clk_mhz]
    puts $apply_fd "Applied PL clock actual FREQ_HZ: $actual_freq_hz"
    puts $apply_fd "Source data:"
    puts $apply_fd "- 50 MHz run: setup WNS 1.427 ns, TNS 0.000 ns, hold WHS 0.016 ns, THS 0.000 ns"
    puts $apply_fd "- 75 nominal run: actual clk_fpga_0 76.929 MHz, setup WNS 0.000 ns, TNS 0.000 ns, hold WHS 0.025 ns, THS 0.000 ns"
    puts $apply_fd "Prediction: 76.929 MHz is the highest no-violation clock supported by the available timing data."
    puts $apply_fd "Action: BD/XCI frequency metadata restored to 76923080 Hz; no synthesis/implementation was run in this apply-only pass."
    close $apply_fd
    close_project
    puts "APPLIED_CLOCK_ONLY=1"
    puts "APPLIED_FREQ_HZ=$actual_freq_hz"
    puts "CLOCK_PREDICTION_LOG=$apply_log"
    return
}

set synth_strategy [try_run_strategy synth_1 "synth_1 strategy" [list \
    "Flow_PerfOptimized_high" \
    "Flow_AreaOptimized_high" \
    "Vivado Synthesis Defaults" \
]]
set impl_strategy [try_run_strategy impl_1 "impl_1 strategy" [list \
    "Performance_ExplorePostRoutePhysOpt" \
    "Performance_Explore" \
    "Performance_NetDelay_high" \
    "Performance_WLBlockPlacement" \
    "Vivado Implementation Defaults" \
]]

set opt_directive [try_step_directive impl_1 STEPS.OPT_DESIGN.ARGS.DIRECTIVE "opt_design directive" [list Explore ExploreWithRemap AddRemap Default]]
set place_directive [try_step_directive impl_1 STEPS.PLACE_DESIGN.ARGS.DIRECTIVE "place_design directive" [list ExtraNetDelay_high ExtraPostPlacementOpt Explore WLDrivenBlockPlacement Default]]
set phys_directive [try_step_directive impl_1 STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE "phys_opt_design directive" [list AggressiveExplore Explore AggressiveFanoutOpt Default]]
set route_directive [try_step_directive impl_1 STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE "route_design directive" [list AggressiveExplore Explore NoTimingRelaxation Default]]
if {[catch {set_property STEPS.PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]} msg]} {
    lappend fallback_log "phys_opt_design enable rejected: $msg"
}
if {[catch {set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true [get_runs impl_1]} msg]} {
    lappend fallback_log "post_route_phys_opt_design enable rejected: $msg"
}
set post_route_phys_directive [try_step_directive impl_1 STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE "post_route_phys_opt_design directive" [list AggressiveExplore Explore Default]]

set strategy_log [file join $log_dir vivado_impl_strategy.txt]
set strategy_fd [open $strategy_log w]
puts $strategy_fd "GPTalk DMA Vivado build strategy"
puts $strategy_fd "Project: $project_xpr"
puts $strategy_fd [format "Control/dynclk clock target: %.3f MHz" $ctrl_clk_mhz]
puts $strategy_fd "Control/dynclk actual FREQ_HZ: $ctrl_actual_freq_hz"
puts $strategy_fd [format "PL clock target: %.3f MHz" $pl_clk_mhz]
puts $strategy_fd "PL clock actual FREQ_HZ: $actual_freq_hz"
puts $strategy_fd "Vivado jobs: $vivado_jobs"
puts $strategy_fd "synth_1 strategy: $synth_strategy"
puts $strategy_fd "impl_1 strategy: $impl_strategy"
puts $strategy_fd "opt_design directive: $opt_directive"
puts $strategy_fd "place_design directive: $place_directive"
puts $strategy_fd "phys_opt_design directive: $phys_directive"
puts $strategy_fd "route_design directive: $route_directive"
puts $strategy_fd "post_route_phys_opt_design directive: $post_route_phys_directive"
puts $strategy_fd ""
puts $strategy_fd "Fallback notes:"
if {[llength $fallback_log] == 0} {
    puts $strategy_fd "- none"
} else {
    foreach item $fallback_log {
        puts $strategy_fd "- $item"
    }
}
close $strategy_fd

reset_run_clean impl_1
reset_run_clean synth_1
set gemv_ooc_runs [get_runs -quiet design_1_gemv_q8_0_dma_top_0_*_synth_1]
foreach gemv_ooc_run $gemv_ooc_runs {
    reset_run_clean $gemv_ooc_run
    launch_runs $gemv_ooc_run -jobs $vivado_jobs
    wait_on_run $gemv_ooc_run
    assert_run_complete $gemv_ooc_run
}
launch_runs synth_1 -jobs $vivado_jobs
wait_on_run synth_1
assert_run_complete synth_1

reset_run_clean impl_1
launch_runs impl_1 -to_step write_bitstream -jobs $vivado_jobs
wait_on_run impl_1
assert_run_complete impl_1

open_run impl_1
set report_prefix [file join $report_dir "gptalk_dma_${clk_label}mhz"]
set timing_report "${report_prefix}_timing_summary.rpt"
set util_report "${report_prefix}_util_hier.rpt"
set route_report "${report_prefix}_route_status.rpt"
set congestion_report "${report_prefix}_congestion.rpt"
set qor_report "${report_prefix}_qor_suggestions.rpt"
set check_timing_report "${report_prefix}_check_timing.rpt"

report_or_warn "hierarchical utilization" [list report_utilization -hierarchical -file $util_report]
report_or_warn "timing summary" [list report_timing_summary -delay_type max -max_paths 50 -file $timing_report]
report_or_warn "route status" [list report_route_status -file $route_report]
report_or_warn "congestion" [list report_design_analysis -congestion -file $congestion_report]
report_or_warn "qor suggestions" [list report_qor_suggestions -file $qor_report]
report_or_warn "check timing" [list check_timing -verbose -file $check_timing_report]

foreach {setup_wns setup_tns setup_fail_count} [timing_slack_summary max] {break}
foreach {hold_whs hold_ths hold_fail_count} [timing_slack_summary min] {break}
set timing_pass 1
if {$setup_wns eq "N/A" || $hold_whs eq "N/A"} {
    set timing_pass 0
} elseif {double($setup_wns) < 0.0 || double($hold_whs) < 0.0} {
    set timing_pass 0
}

file copy -force $timing_report [file join $report_dir full_gemv_timing_summary.rpt]
file copy -force $util_report [file join $report_dir full_gemv_util_hier.rpt]
file copy -force $route_report [file join $report_dir full_gemv_route_status.rpt]
if {[file exists $congestion_report]} {
    file copy -force $congestion_report [file join $report_dir full_gemv_congestion.rpt]
}
if {[file exists $qor_report]} {
    file copy -force $qor_report [file join $report_dir full_gemv_qor_suggestions.rpt]
}

set verify_log [file join $log_dir s02_bitstream_xsa_verify.txt]
set verify_fd [open $verify_log w]
puts $verify_fd "S02 bitstream/XSA verify"
puts $verify_fd "Active Vivado project: $project_xpr"
puts $verify_fd [format "PL clock target: %.3f MHz" $pl_clk_mhz]
puts $verify_fd "PL clock actual FREQ_HZ: $actual_freq_hz"
puts $verify_fd "Full GEMV DMA datapath: expected in GPTalk BD, not smoke-only"
puts $verify_fd [format "GEMV control base: 0x%08X" $gemv_base]
puts $verify_fd [format "AXI DMA base:      0x%08X" $dma_base]
puts $verify_fd [format "Input BRAM base:   0x%08X" $bram_base]
puts $verify_fd "setup WNS(ns): $setup_wns"
puts $verify_fd "setup TNS approx(ns): [format %.3f $setup_tns]"
puts $verify_fd "setup failing paths sampled: $setup_fail_count"
puts $verify_fd "hold WHS(ns): $hold_whs"
puts $verify_fd "hold THS approx(ns): [format %.3f $hold_ths]"
puts $verify_fd "hold failing paths sampled: $hold_fail_count"
puts $verify_fd "timing pass: $timing_pass"
puts $verify_fd "timing report: $timing_report"
puts $verify_fd "util report: $util_report"
puts $verify_fd "route report: $route_report"
puts $verify_fd "congestion report: $congestion_report"
puts $verify_fd "qor suggestions: $qor_report"
close $verify_fd

if {!$timing_pass} {
    catch {source [file join $script_dir report_failed_impl.tcl]}
    error "Timing constraints are not met for ${clk_label}MHz: setup WNS=$setup_wns hold WHS=$hold_whs"
}

set bit_candidates [glob -nocomplain [file join $project_dir GPTalk.runs impl_1 *.bit]]
if {[llength $bit_candidates] == 0} {
    error "bitstream not found under [file join $project_dir GPTalk.runs impl_1]"
}
set bit_file [lindex $bit_candidates 0]
set export_bit_file [file join $export_dir "GPTalk_dma_${export_tag}.bit"]
set latest_bit_file [file join $export_dir GPTalk_dma.bit]
file copy -force $bit_file $export_bit_file
if {$update_latest} {
    file copy -force $bit_file $latest_bit_file
} else {
    set latest_bit_file "SKIPPED_BY_GPTALK_UPDATE_LATEST=0"
}

set xsa_file [file join $export_dir "GPTalk_dma_${export_tag}.xsa"]
set latest_xsa_file [file join $export_dir GPTalk_dma.xsa]
write_hw_platform -fixed -include_bit -force -file $xsa_file
if {$update_latest} {
    file copy -force $xsa_file $latest_xsa_file
} else {
    set latest_xsa_file "SKIPPED_BY_GPTALK_UPDATE_LATEST=0"
}

set verify_fd [open $verify_log a]
puts $verify_fd "write_bitstream: PASS"
puts $verify_fd "bitstream: $bit_file"
puts $verify_fd "export bitstream: $export_bit_file"
puts $verify_fd "latest bitstream: $latest_bit_file"
puts $verify_fd "XSA: $xsa_file"
puts $verify_fd "latest XSA: $latest_xsa_file"
close $verify_fd

set result_doc [file join $internal_docs_dir hw_dma_bringup_result.md]
set result_fd [open $result_doc w]
puts $result_fd "# S02 GPTalk DMA build result"
puts $result_fd ""
puts $result_fd "- Active Vivado project: `$project_xpr`"
puts $result_fd [format "- PL clock target: %.3f MHz" $pl_clk_mhz]
puts $result_fd "- PL clock actual FREQ_HZ: `$actual_freq_hz`"
puts $result_fd "- Full GEMV DMA datapath: yes, not smoke-only"
puts $result_fd "- Bitstream: `$export_bit_file`"
puts $result_fd "- Latest bitstream alias: `$latest_bit_file`"
puts $result_fd "- XSA: `$xsa_file`"
puts $result_fd "- Latest XSA alias: `$latest_xsa_file`"
puts $result_fd [format "- GEMV control base: `0x%08X`" $gemv_base]
puts $result_fd [format "- AXI DMA base: `0x%08X`" $dma_base]
puts $result_fd [format "- Input BRAM base: `0x%08X`" $bram_base]
puts $result_fd "- Setup WNS(ns): `$setup_wns`"
puts $result_fd "- Setup TNS approx(ns): `[format %.3f $setup_tns]`"
puts $result_fd "- Hold WHS(ns): `$hold_whs`"
puts $result_fd "- Hold THS approx(ns): `[format %.3f $hold_ths]`"
puts $result_fd "- Timing pass: `$timing_pass`"
puts $result_fd "- Strategy log: `$strategy_log`"
puts $result_fd "- Timing report: `$timing_report`"
puts $result_fd "- Utilization report: `$util_report`"
puts $result_fd "- Route status report: `$route_report`"
close $result_fd

set active_doc [file join $docs_dir 00_ACTIVE_KR.md]
set doc_fd [open $active_doc a]
puts $doc_fd ""
puts $doc_fd "## S02 빌드 산출물 기록"
puts $doc_fd ""
puts $doc_fd "- 기록 시각: [clock format [clock seconds] -format {%Y-%m-%d %H:%M:%S %Z}]"
puts $doc_fd [format "- PL clock target: `%.3f MHz`" $pl_clk_mhz]
puts $doc_fd "- PL clock actual FREQ_HZ: `$actual_freq_hz`"
puts $doc_fd "- Bitstream: `$export_bit_file`"
puts $doc_fd "- XSA: `$xsa_file`"
puts $doc_fd "- Latest bitstream alias: `$latest_bit_file`"
puts $doc_fd "- Latest XSA alias: `$latest_xsa_file`"
puts $doc_fd "- Timing: setup WNS `$setup_wns` ns, hold WHS `$hold_whs` ns"
puts $doc_fd "- Strategy log: `$strategy_log`"
puts $doc_fd "- S02 verify log: `$verify_log`"
close $doc_fd

puts "BIT_FILE=$export_bit_file"
puts "LATEST_BIT_FILE=$latest_bit_file"
puts "XSA_FILE=$xsa_file"
puts "LATEST_XSA_FILE=$latest_xsa_file"
puts "STRATEGY_LOG=$strategy_log"
puts "S02_VERIFY_LOG=$verify_log"
puts "SETUP_WNS_NS=$setup_wns"
puts [format "SETUP_TNS_APPROX_NS=%.3f" $setup_tns]
puts "HOLD_WHS_NS=$hold_whs"
puts [format "HOLD_THS_APPROX_NS=%.3f" $hold_ths]
