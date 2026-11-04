# Run inside Vivado 2026.1. Rebuilds PL; does not program hardware.
set root [file normalize [file join [file dirname [info script]] ..]]
open_project $root/development/adaptive_raw8_0210/pl/sf21.xpr
reset_run sf21_synth
launch_runs sf21_impl -to_step write_bitstream -jobs 4
wait_on_run sf21_impl
if {[get_property PROGRESS [get_runs sf21_impl]] ne {100%}} {error {Implementation did not finish}}
puts PL_REBUILD_COMPLETE
