if {![info exists ::SF8_CALIBRATION_ANGLE]} {error {Confirm a single known-direction broadband calibration source, then set SF8_CALIBRATION_ANGLE}}
# Set and keep one listening target; optional same-gain comparison.
set root [file normalize [file join [file dirname [info script]] ..]]
set beam_log_name "calibration0212_[clock format [clock seconds] -format %Y%m%d_%H%M%S].txt"
set beam_log [open $root/test_results/$beam_log_name w]
set beam_uart [open {\\.\COM5} r+]
fconfigure $beam_uart -mode 115200,n,8,1 -blocking 0 -buffering none -translation binary -handshake none -eofchar {}
set beam_id 29000
proc beam_query {cmd args_json} {
 global beam_uart beam_log beam_id
 incr beam_id
 puts -nonewline $beam_uart "[format {{"v":1,"id":%d,"cmd":"%s","args":%s}} $beam_id $cmd $args_json]\n"
 flush $beam_uart
 set pending ""; set deadline [expr {[clock milliseconds]+5000}]
 while {[clock milliseconds]<$deadline} {
  append pending [read $beam_uart]
  while {[set n [string first "\n" $pending]]>=0} {
   set line [string trim [string range $pending 0 [expr {$n-1}]]]
   set pending [string range $pending [expr {$n+1}] end]
   if {[string match "*\"id\":$beam_id,*" $line]} {
    puts $beam_log $line;flush $beam_log;puts $line
    if {![string match {*"ok":true*} $line]} {error "Board rejected $cmd"}
    return $line
   }
  }
  after 20;update
 }
 error "UART timeout: $cmd"
}

try {
 set info [beam_query GET_INFO {{}}]
 if {![string match {*0.21.2-eight-mic-localpath*} $info]} {error {Wrong firmware}}
 if {![string is integer -strict $::SF8_CALIBRATION_ANGLE] || $::SF8_CALIBRATION_ANGLE<0 || $::SF8_CALIBRATION_ANGLE>359} {error {Invalid known source angle}}
 beam_query CALIBRATE_PATH8 [format {{"angle_deg":%d}} $::SF8_CALIBRATION_ANGLE]
 set deadline [expr {[clock milliseconds]+20000}]
 set observed_running 0
 while {1} {
  after 200;update
  set reply [beam_query GET_ADAPTIVE_STATUS {{}}]
  if {![regexp {"cal_state":([0-9]+)} $reply -> state]} {error {Missing calibration state}}
  if {$state==1} {set observed_running 1}
  if {$state==2 || $state==3} {
   for {set k 2} {$k<=53} {incr k} {beam_query GET_CALIBRATION_BIN [format {{"bin":%d}} $k]}
   beam_query GET_FILTER_STATUS {{}}
  }
  if {$state==2} {puts "CALIBRATION0212_COMPLETE: $beam_log_name";break}
  if {$state==3} {
   foreach key {cal_bins cal_power_bins cal_coherent_bins cal_amplitude_bins cal_phase_bins} {
    if {![regexp [format {"%s":([0-9]+)} $key] $reply -> $key]} {set $key unknown}
   }
   set reason "Calibration rejected: all gates=$cal_bins/52 (need12); power=$cal_power_bins; coherence=$cal_coherent_bins; amplitude=$cal_amplitude_bins; phase=$cal_phase_bins. Mode1 is directional acoustic-path measurement; stage2 flags bit8 tests held-out repeatability (not a 45deg hardware phase gate). Need12 repeatable bins spanning >=1500Hz. Previous profile retained."
   puts $beam_log $reason;flush $beam_log
   error $reason
  }
  if {[clock milliseconds]>$deadline} {error {Calibration did not complete}}
 }
} finally {close $beam_uart;close $beam_log;unset -nocomplain ::SF8_CALIBRATION_ANGLE}
