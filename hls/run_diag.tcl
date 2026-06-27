open_project -reset diag_test
add_files src/matmul_core.cpp -cflags "-Isrc"
add_files src/matmul_top.cpp   -cflags "-Isrc"
add_files -tb tb/diag_tb.cpp -cflags "-Isrc -Itb"
set_top matmul_top
open_solution -reset solution1
set_part xc7z020clg400-1
create_clock -period 10.0 -name default
csim_design -clean -O
exit
