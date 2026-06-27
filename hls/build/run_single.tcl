# run_single.tcl — 单文件综合脚本
open_project -reset matmul_single
add_files matmul_all.cpp
set_top matmul_top
open_solution -reset solution1
set_part xc7z020clg400-1
create_clock -period 10.0 -name default

puts "\n======= C 综合 ======="
csynth_design

puts "\n======= 导出 IP ======="
# Vivado 2020.2 IP packager 有 bug, 改用 evaluate RTL (RTL已生成, 可直接在Vivado中使用)
# export_design -format ip_catalog ...
catch { export_design -format ip_catalog -vendor "fpga-ai" -version "1.0" } err
puts "IP export result: $err"
puts "RTL 文件位置: solution1/syn/verilog/ 和 solution1/syn/vhdl/"
puts "\n======= 完成! ======="
exit
