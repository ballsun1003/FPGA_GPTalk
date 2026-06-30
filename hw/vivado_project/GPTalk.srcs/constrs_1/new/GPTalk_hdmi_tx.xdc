## Zybo Z7 HDMI TX constraints for design_1 hdmi_out/hdmi_out_ddc ports.
## Pin assignments are from the local Zybo-Z7-Master.xdc HDMI TX section.

set_property -dict { PACKAGE_PIN G17 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { hdmi_out_ddc_scl_io }]
set_property -dict { PACKAGE_PIN G18 IOSTANDARD LVCMOS33 PULLUP true } [get_ports { hdmi_out_ddc_sda_io }]

set_property -dict { PACKAGE_PIN H17 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_clk_n }]
set_property -dict { PACKAGE_PIN H16 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_clk_p }]

set_property -dict { PACKAGE_PIN D20 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_data_n[0] }]
set_property -dict { PACKAGE_PIN D19 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_data_p[0] }]
set_property -dict { PACKAGE_PIN B20 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_data_n[1] }]
set_property -dict { PACKAGE_PIN C20 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_data_p[1] }]
set_property -dict { PACKAGE_PIN A20 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_data_n[2] }]
set_property -dict { PACKAGE_PIN B19 IOSTANDARD TMDS_33 } [get_ports { hdmi_out_data_p[2] }]
