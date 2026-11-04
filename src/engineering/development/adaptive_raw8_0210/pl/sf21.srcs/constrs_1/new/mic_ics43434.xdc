# BX71 schematic page 1: P2 header, Bank 35 (3.3 V).
# P2-1 = GPIO0_0 / L14  -> ICS-43434 SCK
# P2-3 = GPIO0_2 / K14  -> ICS-43434 WS
# P2-5  = GPIO0_4  / H15 <- U1 SD (carrier H1-1)
# P2-7  = GPIO0_6  / F16 <- U2 SD (carrier H1-2)
# P2-9  = GPIO0_8  / E17 <- U3 SD (carrier H1-3)
# P2-13 = GPIO0_10 / B19 <- U4 SD (carrier H1-4)
# P2-11 IS 5 V POWER, NOT AN SD INPUT.
# P2-29 = 3.3 V, P2-30 = GND. Module PS jumper defaults to left.
set_property PACKAGE_PIN L14 [get_ports MIC_SCK]
set_property PACKAGE_PIN K14 [get_ports MIC_WS]
set_property PACKAGE_PIN H15 [get_ports {MIC_SD[0]}]
set_property PACKAGE_PIN F16 [get_ports {MIC_SD[1]}]
set_property PACKAGE_PIN E17 [get_ports {MIC_SD[2]}]
set_property PACKAGE_PIN B19 [get_ports {MIC_SD[3]}]
# Added U5..U8: P2-15/C20, P2-17/D19, P2-19/E18, P2-21/F19.
set_property PACKAGE_PIN C20 [get_ports {MIC_SD[4]}]
set_property PACKAGE_PIN D19 [get_ports {MIC_SD[5]}]
set_property PACKAGE_PIN E18 [get_ports {MIC_SD[6]}]
set_property PACKAGE_PIN F19 [get_ports {MIC_SD[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {MIC_SCK MIC_WS MIC_SD[*]}]
set_property SLEW SLOW [get_ports {MIC_SCK MIC_WS}]
set_property DRIVE 8 [get_ports {MIC_SCK MIC_WS}]
# SD is externally timed to the emitted, fractionally divided SCK. Each input
# has a two-register100MHz synchronizer and is consumed near the following
# SCK rising edge (~160ns after falling edge). Constrain only asynchronous input
# to the first synchronizer stage; never false-path its following stages.
set_false_path -from [get_ports {MIC_SD[*]}] -to [get_cells -hier -filter {NAME =~ *mic_rx/sd_meta_reg*}]
