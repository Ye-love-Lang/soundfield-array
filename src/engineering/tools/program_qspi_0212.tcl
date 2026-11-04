# Persistent BX71 QSPI boot image. Run in Vivado Tcl Console.
# Replaces the boot-image region at flash offset zero; no chip-wide erase.
set sfq_project [file normalize [file join [file dirname [info script]] ..]]
set sfq_root $sfq_project/development/qspi_boot_0212
set sfq_build $sfq_root/build
foreach {name crc} {BOOT.bin 2772263000 fsbl_qspi.elf 2333170667 soundfield_sectorfir.bit 1039615890 soundfield_sectorfir.elf 1443062632} {
 set f [open $sfq_build/$name rb];set actual [zlib crc32 [read $f]];close $f
 if {$actual!=$crc} {error "Boot artifact mismatch: $name"}
}
if {[file size $sfq_build/BOOT.bin]>16777216} {error {BOOT.bin exceeds BX71 flash capacity}}
if {![file exists $sfq_root/backup_verified.txt] || ![file exists $sfq_root/flash_before_0212.bin]} {
 error {Verified original Flash backup is missing. Do not proceed.}
}
catch {open_hw_manager}
if {[llength [get_hw_servers -quiet]]==0} {connect_hw_server -allow_non_jtag}
lappend auto_path {D:/AMD_DesignTools/2026.1/Vivado/scripts/xsdb}
package require xsdb
::xsdb::connect -url tcp:127.0.0.1:3121
::xsdb::targets -set -filter {name =~ "*APU*"}
set mode [::xsdb::mrd -value 0xF800025C]
if {($mode & 15)!=0} {error {Set both BX71 boot switches UP and power-cycle for JTAG mode before programming}}
# Release debugger/hardware-manager claims; the programming tool owns JTAG.
catch {::xsdb::disconnect}
foreach t [get_hw_targets -quiet] {catch {close_hw_target $t}}
set sfq_stamp [clock format [clock seconds] -format %Y%m%d_%H%M%S]
set sfq_log $sfq_project/test_results/qspi_program0212_$sfq_stamp.txt
set sfq_exe {D:/AMD_DesignTools/2026.1/Vitis/bin/unwrapped/win64.o/rdi_zynq_flash.exe}
set sfq_saved_env {}
foreach {key value} {
 RDI_DATADIR D:/AMD_DesignTools/2026.1/Vitis/data
 RDI_APPROOT D:/AMD_DesignTools/2026.1/Vitis
 XILINX_VITIS D:/AMD_DesignTools/2026.1/Vitis
} {
 if {[info exists ::env($key)]} {dict set sfq_saved_env $key [list 1 $::env($key)]} else {dict set sfq_saved_env $key [list 0 {}]}
 set ::env($key) $value
}
puts "Programming and verifying BOOT.bin. Keep power and JTAG connected. Log: $sfq_log"
try {
 set rc [catch {exec -- $sfq_exe -f $sfq_build/BOOT.bin -offset 0 -flash_type qspi-x4-single -fsbl $sfq_build/fsbl_qspi.elf -blank_check -verify -url tcp:127.0.0.1:3121 > $sfq_log 2>@1} result options]
} finally {
 dict for {key saved} $sfq_saved_env {
  if {[lindex $saved 0]} {set ::env($key) [lindex $saved 1]} else {unset ::env($key)}
 }
}
set f [open $sfq_log r];set sfq_output [read $f];close $f
puts $sfq_output
if {$rc} {return -options $options "QSPI programming failed: $result. Inspect $sfq_log"}
if {![regexp -nocase {Flash.*(programmed|programming).*success|programming.*success} $sfq_output]} {
 error "Tool exited without expected programming-success text; inspect $sfq_log"
}
if {![regexp -nocase {verify.*success|verification.*success} $sfq_output]} {
 error "Verification success not confirmed; inspect $sfq_log"
}
set f [open $sfq_root/flash_programmed_verified.txt w]
puts $f "version=0.21.2-eight-mic-localpath boot_crc32=2772263000 log=$sfq_log"
close $f
puts "QSPI_0212_PROGRAM_AND_VERIFY_COMPLETE: $sfq_log"
puts {Next: power OFF, set QSPI boot switches per BX71 manual, then power ON. Do not run the JTAG download script for cold-boot validation.}
