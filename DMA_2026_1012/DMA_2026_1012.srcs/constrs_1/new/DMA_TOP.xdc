# Constraints adapted from the original zu47dr.xdc for DMA_TOP.
# Only constraints corresponding to ports and clock paths retained by DMA_TOP
# are included.

# Register interface clock, inherited from the original project.
create_clock -period 10.000 -name reg_clk -waveform {0.000 5.000} [get_ports reg_clk]

# The PS PL clock and the external 312.5 MHz processing clock are asynchronous.
# This is the original project's clock-group constraint with the hierarchy
# updated for AXI_DMA_wrapper in DMA_TOP.
set_clock_groups -asynchronous -group [get_clocks *clk_pl_0*] -group [get_clocks -of_objects [get_pins u0_AXI_DMA_wrapper/AXI_DMA_i/clk_wiz_0/inst/mmcme4_adv_inst/CLKOUT0]]

# PL differential clock and MTS SYSREF locations from the original board XDC.
set_property PACKAGE_PIN E12 [get_ports CLK_DIFF_2_SYSREF_clk_p]
set_property PACKAGE_PIN E14 [get_ports CLK_DIFF_1_PL_CLK_clk_p]
set_property IOSTANDARD SUB_LVDS [get_ports CLK_DIFF_1_PL_CLK_clk_p]
set_property IOSTANDARD SUB_LVDS [get_ports CLK_DIFF_2_SYSREF_clk_p]

# Register address bus.
set_property PACKAGE_PIN AH18 [get_ports {reg_addr[0]}]
set_property PACKAGE_PIN AJ18 [get_ports {reg_addr[1]}]
set_property PACKAGE_PIN AK18 [get_ports {reg_addr[2]}]
set_property PACKAGE_PIN AL18 [get_ports {reg_addr[3]}]
set_property PACKAGE_PIN AH13 [get_ports {reg_addr[4]}]
set_property PACKAGE_PIN AJ13 [get_ports {reg_addr[5]}]
set_property PACKAGE_PIN AN18 [get_ports {reg_addr[6]}]
set_property PACKAGE_PIN AP17 [get_ports {reg_addr[7]}]
set_property PACKAGE_PIN AM17 [get_ports {reg_addr[8]}]
set_property PACKAGE_PIN AN17 [get_ports {reg_addr[9]}]
set_property PACKAGE_PIN AL17 [get_ports {reg_addr[10]}]
set_property PACKAGE_PIN AM16 [get_ports {reg_addr[11]}]

set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[0]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[1]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[2]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[3]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[4]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[5]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[6]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[7]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[8]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[9]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[10]}]
set_property IOSTANDARD LVCMOS12 [get_ports {reg_addr[11]}]

# Register read/write strobes and clock.
set_property PACKAGE_PIN AN15 [get_ports reg_wr_en]
set_property IOSTANDARD LVCMOS12 [get_ports reg_wr_en]

set_property PACKAGE_PIN AM15 [get_ports reg_rd_en]
set_property IOSTANDARD LVCMOS12 [get_ports reg_rd_en]

set_property PACKAGE_PIN AM14 [get_ports reg_clk]
set_property IOSTANDARD LVCMOS12 [get_ports reg_clk]

# Register data bus.
set_property PACKAGE_PIN D13 [get_ports {reg_data[0]}]
set_property PACKAGE_PIN C13 [get_ports {reg_data[1]}]
set_property PACKAGE_PIN H15 [get_ports {reg_data[2]}]
set_property PACKAGE_PIN H14 [get_ports {reg_data[3]}]
set_property PACKAGE_PIN K15 [get_ports {reg_data[4]}]
set_property PACKAGE_PIN K14 [get_ports {reg_data[5]}]
set_property PACKAGE_PIN H10 [get_ports {reg_data[6]}]
set_property PACKAGE_PIN H9 [get_ports {reg_data[7]}]
set_property PACKAGE_PIN D9 [get_ports {reg_data[8]}]
set_property PACKAGE_PIN C9 [get_ports {reg_data[9]}]
set_property PACKAGE_PIN A10 [get_ports {reg_data[10]}]
set_property PACKAGE_PIN A9 [get_ports {reg_data[11]}]
set_property PACKAGE_PIN C10 [get_ports {reg_data[12]}]
set_property PACKAGE_PIN B10 [get_ports {reg_data[13]}]
set_property PACKAGE_PIN C11 [get_ports {reg_data[14]}]
set_property PACKAGE_PIN B11 [get_ports {reg_data[15]}]

set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[15]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[14]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[13]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[12]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[11]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[10]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[9]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[8]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[7]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[6]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[5]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[4]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[3]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[2]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[1]}]
set_property IOSTANDARD LVCMOS18 [get_ports {reg_data[0]}]

set_property DRIVE 16 [get_ports {reg_data[15]}]
set_property DRIVE 16 [get_ports {reg_data[14]}]
set_property DRIVE 16 [get_ports {reg_data[13]}]
set_property DRIVE 16 [get_ports {reg_data[12]}]
set_property DRIVE 16 [get_ports {reg_data[11]}]
set_property DRIVE 16 [get_ports {reg_data[10]}]
set_property DRIVE 16 [get_ports {reg_data[9]}]
set_property DRIVE 16 [get_ports {reg_data[8]}]
set_property DRIVE 16 [get_ports {reg_data[7]}]
set_property DRIVE 16 [get_ports {reg_data[6]}]
set_property DRIVE 16 [get_ports {reg_data[5]}]
set_property DRIVE 16 [get_ports {reg_data[4]}]
set_property DRIVE 16 [get_ports {reg_data[3]}]
set_property DRIVE 16 [get_ports {reg_data[2]}]
set_property DRIVE 16 [get_ports {reg_data[1]}]
set_property DRIVE 16 [get_ports {reg_data[0]}]

# Clock-route exception present in the original project. The instance path is
# updated to match the current wrapper and util_ds_buf instance name.
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets {u0_AXI_DMA_wrapper/AXI_DMA_i/util_ds_buf_1/U0/USE_IBUFDS.GEN_IBUFDS[0].IBUFDS_I/O}]

