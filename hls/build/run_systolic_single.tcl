# Vitis HLS 2020.2 synthesis uses the flattened source because its front-end
# mishandles local includes and macros in pragma arguments.
open_project -reset systolic_single
add_files matmul_systolic_all.cpp
set_top matmul_top
open_solution -reset solution1
set_part xc7z020clg400-2
create_clock -period 10.0 -name default
csynth_design
export_design -format ip_catalog -vendor fpga-ai -version 1.0
exit
