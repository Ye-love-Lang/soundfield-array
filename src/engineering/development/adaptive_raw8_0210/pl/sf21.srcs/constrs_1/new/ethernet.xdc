
# BX71 Zynq-7020 / YT8531 Ethernet
# Vivado 2026.1

# RGMII clocks
set_property PACKAGE_PIN H17 [get_ports RGMII_0_txc]
set_property PACKAGE_PIN H16 [get_ports RGMII_0_rxc]

# RGMII transmit data
set_property PACKAGE_PIN J16 [get_ports {RGMII_0_td[0]}]
set_property PACKAGE_PIN G18 [get_ports {RGMII_0_td[1]}]
set_property PACKAGE_PIN K16 [get_ports {RGMII_0_td[2]}]
set_property PACKAGE_PIN J15 [get_ports {RGMII_0_td[3]}]

# RGMII receive data
set_property PACKAGE_PIN M15 [get_ports {RGMII_0_rd[0]}]
set_property PACKAGE_PIN M17 [get_ports {RGMII_0_rd[1]}]
set_property PACKAGE_PIN M14 [get_ports {RGMII_0_rd[2]}]
set_property PACKAGE_PIN N16 [get_ports {RGMII_0_rd[3]}]

# RGMII control signals
set_property PACKAGE_PIN G14 [get_ports RGMII_0_tx_ctl]
set_property PACKAGE_PIN M18 [get_ports RGMII_0_rx_ctl]

# PHY management interface
set_property PACKAGE_PIN R17 [get_ports MDIO_PHY_0_mdc]
set_property PACKAGE_PIN R19 [get_ports MDIO_PHY_0_mdio_io]

# External PHY reset
set_property PACKAGE_PIN G17 [get_ports {ETH_RESET[0]}]

# I/O standards
set_property IOSTANDARD LVCMOS33 [get_ports {
    RGMII_0_txc RGMII_0_rxc
    RGMII_0_td[*] RGMII_0_rd[*]
    RGMII_0_tx_ctl RGMII_0_rx_ctl
    MDIO_PHY_0_mdc MDIO_PHY_0_mdio_io
    ETH_RESET[*]
}]

# 125 MHz RGMII receive clock
create_clock -name rgmii_rxc -period 8.000 [get_ports RGMII_0_rxc]
