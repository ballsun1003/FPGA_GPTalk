set repo_root [file normalize [lindex $argv 0]]
set xsa_path [file normalize [lindex $argv 1]]
set out_dir [file normalize [lindex $argv 2]]

file mkdir $out_dir

hsi::open_hw_design $xsa_path
hsi::set_repo_path [file join $repo_root artifacts s04_linux_dt device-tree-xlnx]
hsi::create_sw_design device-tree -os device_tree -proc ps7_cortexa9_0
hsi::set_property CONFIG.console_device ps7_uart_1 [hsi::get_os]
hsi::set_property CONFIG.main_memory ps7_ddr_0 [hsi::get_os]
hsi::set_property CONFIG.bootargs "console=ttyPS0,115200 earlyprintk uio_pdrv_genirq.of_id=generic-uio root=/dev/mmcblk0p2 rw rootwait" [hsi::get_os]
hsi::generate_target -dir $out_dir
hsi::close_hw_design [hsi::current_hw_design]
