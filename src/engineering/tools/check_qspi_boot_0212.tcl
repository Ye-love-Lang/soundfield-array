# Read-only runtime check after a real OFF/ON in QSPI mode.
# No PS initialization, reset, FPGA download, ELF download, or continue command.
set sfq_project [file normalize [file join [file dirname [info script]] ..]]
lappend auto_path {D:/AMD_DesignTools/2026.1/Vivado/scripts/xsdb}
package require xsdb
::xsdb::connect -url tcp:127.0.0.1:3121
::xsdb::targets -set -filter {name =~ "*APU*"}
set mode [::xsdb::mrd -value 0xF800025C]
if {($mode & 15)!=1} {error "Boot-mode register is $mode; expected QSPI=1 after OFF/ON"}
set SF8_UART_LOG "qspi_coldboot0212_[clock format [clock seconds] -format %Y%m%d_%H%M%S].txt"
set SF8_UART_COMMANDS {PING GET_INFO GET_NET_DIAG GET_FILTER_STATUS GET_ADAPTIVE_STATUS}
set SF8_UART_SAMPLES 5
set SF8_UART_INTERVAL 250
set SF8_UART_SAMPLE_COMMANDS {GET_DOA_STATUS GET_FILTER_STATUS}
source $sfq_project/tools/check_uart_vivado_native.tcl
set f [open $sfq_project/test_results/$SF8_UART_LOG r];set data [read $f];close $f
foreach marker {{0.21.2-eight-mic-localpath} {"spatial_active":1} {"spatial_failures":0} {"raw_supported":1} {"fresh":true}} {
 if {[string first $marker $data]<0} {error "Cold boot not verified: missing $marker; inspect $SF8_UART_LOG"}
}
if {[regexp {"spatial_failures":[1-9]} $data]} {error "Cold boot FIR fault; inspect $SF8_UART_LOG"}
set f [open $sfq_project/development/qspi_boot_0212/cold_boot_verified.txt w]
puts $f "boot_mode=$mode version=0.21.2-eight-mic-localpath log=$SF8_UART_LOG"
close $f
puts "QSPI_0212_COLD_BOOT_RUNTIME_VERIFIED: $SF8_UART_LOG"
