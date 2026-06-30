# Directly program the active GPTalk bitstream through Vivado Hardware Manager.
# Does not modify the Vivado project or generate hardware.

set script_dir [file normalize [file dirname [info script]]]
set repo_root [file normalize [file join $script_dir ..]]
set bit_file [file join $repo_root hw vivado_project export GPTalk_dma.bit]
set log_dir [file join $repo_root logs]
set result_file [file join $log_dir hw_direct_program_result.txt]
file mkdir $log_dir

proc write_result {path text} {
    set fd [open $path w]
    puts -nonewline $fd $text
    close $fd
}

set result "GPTalk direct bitstream program result\n"
append result "Date: 2026-06-30 KST\n"
append result "Bitstream: $bit_file\n"

if {![file exists $bit_file]} {
    append result "Result: FAIL\nReason: bitstream missing\n"
    write_result $result_file $result
    error "bitstream missing: $bit_file"
}

if {[catch {
    open_hw_manager
    connect_hw_server
    set targets [get_hw_targets -quiet *]
    append result "Targets: $targets\n"
    if {[llength $targets] == 0} {
        error "no hardware targets found"
    }
    foreach target $targets {
        if {[catch {open_hw_target $target} target_msg]} {
            append result "open_hw_target failed for $target: $target_msg\n"
        } else {
            append result "Opened target: $target\n"
            break
        }
    }
    set devices [get_hw_devices -quiet]
    append result "Devices: $devices\n"
    if {[llength $devices] == 0} {
        error "no hardware devices found after opening target"
    }
    set dev ""
    foreach candidate $devices {
        set part ""
        catch {set part [get_property PART $candidate]}
        if {[string match -nocase "*xc7z020*" $part] || [string match -nocase "*xc7z020*" $candidate]} {
            set dev $candidate
            break
        }
    }
    if {$dev eq ""} {
        set dev [lindex $devices 0]
    }
    current_hw_device $dev
    refresh_hw_device $dev
    append result "Selected device: $dev\n"
    foreach prop [list PART DEVICE_ID PROGRAM.FILE PROGRAM.HW_CFGMEM] {
        set value "<unavailable>"
        catch {set value [get_property $prop $dev]}
        append result "$prop: $value\n"
    }
    set_property PROGRAM.FILE $bit_file $dev
    program_hw_devices $dev
    refresh_hw_device $dev
    append result "Result: PASS\n"
    append result "Vivado program_hw_devices completed. Confirm physical DONE LED on board.\n"
} err]} {
    append result "Result: FAIL\n"
    append result "Reason: $err\n"
    write_result $result_file $result
    error $err
}

write_result $result_file $result
close_hw_manager
