# Functional C simulation from the readable multi-file source.
open_project -reset systolic_csim
add_files src/matmul_systolic_core.cpp -cflags "-Isrc"
add_files src/matmul_systolic_top.cpp -cflags "-Isrc"
add_files -tb tb/matmul_tb.cpp -cflags "-Isrc"
set_top matmul_top
open_solution -reset solution1
set_part xc7z020clg400-2
create_clock -period 10.0 -name default
if {[catch {csim_design -clean} message]} {
    puts stderr $message
    exit 1
}
exit
