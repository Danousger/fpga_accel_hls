# run.tcl — 展平目录构建脚本
open_project -reset matmul_accel
# Windows 绝对路径 include (clang-tidy 从 solution 目录运行, 相对路径会解析错)
add_files matmul_core.cpp -cflags "-IE:/fpga-ai/hls/build"
add_files matmul_top.cpp   -cflags "-IE:/fpga-ai/hls/build"
add_files -tb matmul_tb.cpp -cflags "-IE:/fpga-ai/hls/build"
set_top matmul_top
open_solution -reset solution1
set_part xc7z020clg400-1
create_clock -period 10.0 -name default

puts "\n======= C 仿真 ======="
csim_design -clean -O

puts "\n======= C 综合 ======="
csynth_design

puts "\n======= 导出 IP ======="
export_design -format ip_catalog \
    -description "定点数矩阵乘法加速器 (Q8.8, 48bit累加器, 4MAC并行, Tile 16x16x16)" \
    -vendor "fpga-ai" -version "1.0"

puts "\n======= 完成! ======="
exit
