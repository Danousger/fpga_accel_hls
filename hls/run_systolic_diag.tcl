# run_systolic_diag.tcl — 脉动阵列快速功能验证 (csim only)
open_project -reset systolic_diag
add_files src/matmul_systolic_core.cpp -cflags "-Isrc"
add_files src/matmul_systolic_top.cpp   -cflags "-Isrc"
add_files -tb tb/diag_tb.cpp            -cflags "-Isrc -Itb"
add_files src/matmul.h          -cflags "-Isrc"
add_files src/matmul_systolic.h -cflags "-Isrc"
add_files src/matmul_top.h      -cflags "-Isrc"
set_top matmul_top
open_solution -reset solution1
set_part xc7z020clg400-2
create_clock -period 10.0 -name default
csim_design -clean -O
exit
