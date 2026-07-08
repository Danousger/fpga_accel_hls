# run_systolic_single.tcl — 脉动阵列单文件综合脚本
# 用法: cd /e/fpga-ai/hls/build && vitis_hls -f run_systolic_single.tcl
#
# 10ns (100MHz) 时钟 — 脉动阵列 II=1 目标
open_project -reset systolic_single
add_files matmul_systolic_all.cpp
set_top matmul_top
open_solution -reset solution1
set_part xc7z020clg400-2
create_clock -period 10.0 -name default

puts "\n======= C 综合 ======="
csynth_design

puts "\n======= 导出 IP ======="
export_design -format ip_catalog -vendor "fpga-ai" -version "1.0"
puts "\[INFO\] IP 导出成功!"
puts "RTL 文件: solution1/syn/verilog/ 和 solution1/syn/vhdl/"
puts "\n======= 完成! ======="
exit
