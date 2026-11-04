# Native Vivado Tcl serial diagnostics. No shell or external executable.
set sf8_uart_root [file normalize [file join [file dirname [info script]] ..]]
set sf8_log_name "hardware_uart_[clock format [clock seconds] -format %Y%m%d_%H%M%S].txt"
if {[info exists ::SF8_UART_LOG]} {set sf8_log_name $::SF8_UART_LOG}
set sf8_uart_log [open [file join $sf8_uart_root test_results $sf8_log_name] w]
set sf8_commands {PING GET_INFO GET_FILTER_STATUS GET_NET_DIAG GET_STATUS GET_SPECTRUM}
if {[info exists ::SF8_UART_COMMANDS]} {set sf8_commands $::SF8_UART_COMMANDS}
set sf8_samples 15
if {[info exists ::SF8_UART_SAMPLES]} {set sf8_samples $::SF8_UART_SAMPLES}
set sf8_interval 400
if {[info exists ::SF8_UART_INTERVAL]} {set sf8_interval $::SF8_UART_INTERVAL}
set sf8_sample_commands {GET_DOA_STATUS GET_SOURCES}
if {[info exists ::SF8_UART_SAMPLE_COMMANDS]} {set sf8_sample_commands $::SF8_UART_SAMPLE_COMMANDS}
set sf8_uart [open {\\.\COM5} r+]
fconfigure $sf8_uart -mode 115200,n,8,1 -blocking 0 -buffering none -translation binary -handshake none -eofchar {}
set sf8_uart_id 4000
proc sf8_uart_query {command} {
    global sf8_uart sf8_uart_log sf8_uart_id
    incr sf8_uart_id
    # Compose JSON without interpreting Tcl substitutions in its contents.
    set request [format {{"v":1,"id":%d,"cmd":"%s","args":{}}} $sf8_uart_id $command]
    puts -nonewline $sf8_uart "$request\n"
    flush $sf8_uart
    set pending ""
    set deadline [expr {[clock milliseconds]+5000}]
    while {[clock milliseconds] < $deadline} {
        append pending [read $sf8_uart]
        while {[set at [string first "\n" $pending]] >= 0} {
            set line [string trim [string range $pending 0 [expr {$at-1}]]]
            set pending [string range $pending [expr {$at+1}] end]
            if {$line ne ""} {puts $sf8_uart_log $line; flush $sf8_uart_log; puts $line}
            if {[string match "*\"id\":$sf8_uart_id,*" $line]} {return}
        }
        after 20
        update
    }
    error "No matching $command response on COM5"
}
try {
    read $sf8_uart
    foreach command $sf8_commands {sf8_uart_query $command}
    for {set sample 0} {$sample < $sf8_samples} {incr sample} {
        foreach command $sf8_sample_commands {sf8_uart_query $command}
        after $sf8_interval
    }
    puts "ARRAY8_UART_DIAGNOSTICS_COMPLETE"
} finally {
    close $sf8_uart
    close $sf8_uart_log
}
