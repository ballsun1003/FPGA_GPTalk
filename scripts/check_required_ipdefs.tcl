set patterns {
  digilentinc.com:ip:rgb2dvi:*
  digilentinc.com:ip:dvi2rgb:*
  digilentinc.com:ip:axi_dynclk:*
  digilentinc.com:IP:PWM:*
  analog.com:user:axi_i2s_adi:*
  xilinx.com:ip:v_tc:*
  xilinx.com:ip:axi_vdma:*
  xilinx.com:ip:v_axi4s_vid_out:*
  xilinx.com:ip:v_frmbuf_wr:*
  xilinx.com:ip:processing_system7:*
  xilinx.com:ip:axi_dma:*
}

foreach pattern $patterns {
    set defs [get_ipdefs -quiet $pattern]
    puts [format "%s => %d" $pattern [llength $defs]]
    foreach def $defs {
        puts [format "  %s" $def]
    }
}
