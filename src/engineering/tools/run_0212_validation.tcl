# Volatile JTAG candidate: geometric sector FIR; this is not a flash image.
set sf20_root [file normalize [file join [file dirname [info script]] ..]]
set sf20_build $sf20_root/development/directional_path_0212/build
foreach {name expected} {
 soundfield_sectorfir.bit 1039615890
 soundfield_sectorfir.xsa 2251531722
 soundfield_sectorfir.elf 1443062632
 ps7_init.tcl 1452454696
} {
 set fd [open $sf20_build/$name rb];set actual [zlib crc32 [read $fd]];close $fd
 if {$actual!=$expected} {error "Candidate artifact mismatch: $name"}
}
catch {open_hw_manager}
if {[llength [get_hw_servers -quiet]]==0} {catch {connect_hw_server -allow_non_jtag}}
lappend auto_path {D:/AMD_DesignTools/2026.1/Vivado/scripts/xsdb}
package require xsdb
::xsdb::connect -url tcp:127.0.0.1:3121
puts [::xsdb::targets]
::xsdb::targets -set -filter {name =~ "*APU*"}
::xsdb::rst -system
after 3000
::xsdb::targets -set -filter {name =~ "*xc7z020*"}
::xsdb::fpga -file $sf20_build/soundfield_sectorfir.bit
::xsdb::configparams force-mem-access 1
try {
 ::xsdb::targets -set -filter {name =~ "*APU*"}
 namespace eval ::xsdb [list source $sf20_build/ps7_init.tcl]
 ::xsdb::ps7_init
 ::xsdb::ps7_post_config
 ::xsdb::targets -set -filter {name =~ "*A9*#0*"}
 ::xsdb::rst -processor
 ::xsdb::dow $sf20_build/soundfield_sectorfir.elf
 ::xsdb::con
} finally {::xsdb::configparams force-mem-access 0}
puts SECTORFIR_JTAG_DOWNLOAD_COMPLETE
after 8000
set SF8_UART_LOG "hardware_0212_validation_[clock format [clock seconds] -format %Y%m%d_%H%M%S].txt"
set SF8_UART_COMMANDS {PING GET_INFO GET_NET_DIAG GET_FILTER_STATUS GET_ADAPTIVE_STATUS}
set SF8_UART_SAMPLES 20
set SF8_UART_INTERVAL 200
set SF8_UART_SAMPLE_COMMANDS {GET_DOA_STATUS GET_FILTER_STATUS GET_ADAPTIVE_STATUS}
source $sf20_root/tools/check_uart_vivado_native.tcl
set fd [open $sf20_root/test_results/$SF8_UART_LOG r];set log [read $fd];close $fd
foreach marker {{0.21.2-eight-mic-localpath} {"spatial_supported":1} {"spatial_active":1} {"spatial_failures":0}} {
 if {[string first $marker $log]<0} {error "Download completed but runtime verification missing $marker; inspect $SF8_UART_LOG"}
}
if {[regexp {"spatial_failures":[1-9]} $log]} {error {Spatial runtime fault; inspect UART log}}
if {![regexp {"raw_supported":1} $log] || ![regexp {"raw_records":[1-9]} $log]} {error {Raw capture hardware did not produce frames}}
if {[regexp {"raw_errors":[1-9]} $log]} {error {Raw capture errors; inspect log}}
puts "LOADING_0212_VALIDATION_COMPLETE: $SF8_UART_LOG"
puts {Reconnect phone App to 192.168.77.10:7000. Headphone audio and acoustic suppression require separate testing.}
