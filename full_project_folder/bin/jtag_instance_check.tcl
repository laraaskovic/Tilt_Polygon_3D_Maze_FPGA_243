proc compare_node_number {arg1 arg2} {
   if {[regexp {.*?node_(\d+)/.*?$} $arg1 -> n1] && [regexp {.*?node_(\d+)/.*?$} $arg2 -> n2]} {
       return [expr {$n1 - $n2}]
   }
   return [string compare $arg1 $arg2]
}

# INPUTS (cable name (just the USB-X), project directory, and the JTAG UART name)
set cable_name [lindex $::argv 0]
set project_dir [lindex $::argv 1]
set jtag_uart_name ""
set cpu_name ""

if {$::argc >= 4} {
    if {[string equal [lindex $::argv 2] "-jtaguart"]} {
        set jtag_uart_name [lindex $::argv 3]
    } elseif {[string equal [lindex $::argv 2] "-cpu"]} {
        set cpu_name [lindex $::argv 3]
    }
}
if {$::argc >= 6} {
    if {[string equal [lindex $::argv 4] "-jtaguart"]} {
        set jtag_uart_name [lindex $::argv 5]
    } elseif {[string equal [lindex $::argv 4] "-cpu"]} {
        set cpu_name [lindex $::argv 5]
    }
}

set device_service_paths [get_service_paths device]
set device_service_path [lindex $device_service_paths [lsearch $device_service_paths "*$cable_name*"]]
set design_instance_path [design_load $project_dir]
design_link $design_instance_path $device_service_path

# PRINT OUT INSTANCE ID INFO FOR EVERYTHING:
set i 0
foreach path [lsort -command compare_node_number [get_service_paths bytestream]] {
    # If this path corresponds to a JTAG UART, incr i
    if {[string match *$cable_name* $path ] && [string match *jtag_uart* [marker_get_type $path] ]} {
        puts "[marker_get_info $path] (INSTANCE_ID:$i)"
        incr i
    }
}
set i 0
foreach path [lsort -command compare_node_number [get_service_paths processor]] {
     # If this path corresponds to a NiosII, incr i
    if {[string match *$cable_name* $path ] && [string match *nios2* [marker_get_type $path] ]} {
        puts "[marker_get_info $path] (INSTANCE_ID:$i)"
        incr i
    }
}


# # IF USING NAMES:
# set i 0
# set jtaguart_found false
# if {[string length $jtag_uart_name] > 0} {
    # foreach path [lsort -command compare_node_number [get_service_paths bytestream]] {
        # # if this node is on our device's path, and it contains our jtag uart's name at the end
        # if {[string match *$device_service_path* $path ] && [string match */$jtag_uart_name.jtag $path ]} {
            # if {$jtaguart_found == true} {
                # puts "WARNING: Multiple JTAG UARTs with same name found. Using the one that was found first."
            # } else {
                # puts JTAGUART_INSTANCE_ID:$i
                # set jtaguart_found true
            # }
        # }
        # # If this path corresponds to a JTAG UART, incr i
        # if {[string match *jtag_uart* [marker_get_type $path] ]} {
            # incr i
        # }
    # }
# }
# set i 0
# set cpu_found false
# if {[string length $cpu_name] > 0} {
    # foreach cpu [lsort -command compare_node_number [get_service_paths processor]] {
        # if {[string match *$device_service_path* $cpu ] && [string match *$cpu_name.data_master $cpu]} {
            # if {$cpu_found == true} {
                # puts "WARNING: Multiple CPUs with same name found. Using the one that was found first."
            # } else {
                # puts CPU_INSTANCE_ID:$i
                # set cpu_found true
            # }
        # }
         # # If this path corresponds to a NiosII, incr i
        # if {[string match *nios2* [marker_get_type $cpu] ]} {
            # incr i
        # }
    # }
# }

# IF USING HPATHS:
# set i 0
# foreach path [get_service_paths bytestream] {
    # # if this node is on our device's path, and it contains our jtag uart's name at the end
    # # puts $path
    # if {[string match *$device_service_path* $path ] && [string match *$jtag_uart_name [marker_get_info $path] ]} {
        # #puts $path
        # puts JTAGUART_INSTANCE_ID:$i
    # }
    # # If this path corresponds to a JTAG UART, incr i
    # if {[string match *jtag_uart* [marker_get_type $path] ]} {
        # incr i
    # }
# }
# set i 0
# foreach cpu [lsort -command compare_node_number [get_service_paths processor]] {
    # # puts $cpu
    # # puts [marker_get_info $cpu]
    # if {[string match *$device_service_path* $path ] && [string match *$cpu_name* [marker_get_info $cpu] ]} {
        # puts CPU_INSTANCE_ID:$i
    # }
    # incr i
# }