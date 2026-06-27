# ============================================================================
# run_hls.tcl — Vitis HLS 一键运行脚本
# ============================================================================

set PROJ_NAME   "matmul_accel"
set TOP_FN      "matmul_top"
set CLK_PERIOD  10.0
set PART        "xc7z020clg400-2"

# 项目放在当前目录, 不 cd
open_project -reset ${PROJ_NAME}

add_files src/matmul_core.cpp -cflags "-Isrc"
add_files src/matmul_top.cpp   -cflags "-Isrc"
add_files -tb tb/matmul_tb.cpp -cflags "-Isrc -Itb"

# 显式添加头文件 + 设置全局 include 路径 (确保仿真和综合都能找到)
add_files src/matmul.h      -cflags "-Isrc"
add_files src/matmul_core.h -cflags "-Isrc"
add_files src/matmul_top.h  -cflags "-Isrc"

set_top ${TOP_FN}
open_solution -reset solution1

# 全局 C 编译标志 (综合 + 仿真均使用)
set_directive_compile -flags "-Isrc" matmul_top
set_part ${PART}
create_clock -period ${CLK_PERIOD} -name default

puts "\n======= 步骤 1/4: C 仿真 ======="
csim_design -clean -O

puts "\n======= 步骤 2/4: C 综合 ======="
csynth_design

puts "\n======= 步骤 3/4: 导出 IP ======="
export_design -format ip_catalog \
    -description "定点数矩阵乘法加速器 (Q8.8, 48bit累加器, Tile 16x16x16, 4 MAC并行)" \
    -vendor "fpga-ai" -version "1.0"

puts "\n======= 完成! ======="
