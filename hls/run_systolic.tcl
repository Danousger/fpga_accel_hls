# ============================================================================
# run_systolic.tcl — 脉动阵列版 HLS 一键脚本
#
# 用法: cd /e/fpga-ai/hls && vitis_hls -f run_systolic.tcl
#
# 源文件: matmul_systolic_core.cpp + matmul_systolic_top.cpp (替代原版 matmul_core)
# 测试:   复用 tb/matmul_tb.cpp (接口不变) + tb/diag_tb.cpp
# ============================================================================

set PROJ_NAME   "systolic_accel"
set TOP_FN      "matmul_top"
set CLK_PERIOD  10.0
set PART        "xc7z020clg400-2"

open_project -reset ${PROJ_NAME}

# 脉动阵列源文件 (替代原 matmul_core.cpp + matmul_top.cpp)
add_files src/matmul_systolic_core.cpp -cflags "-Isrc"
add_files src/matmul_systolic_top.cpp   -cflags "-Isrc"
add_files -tb tb/matmul_tb.cpp          -cflags "-Isrc -Itb"

# 头文件
add_files src/matmul.h          -cflags "-Isrc"
add_files src/matmul_systolic.h -cflags "-Isrc"
add_files src/matmul_top.h      -cflags "-Isrc"

set_top ${TOP_FN}
open_solution -reset solution1

set_part ${PART}
create_clock -period ${CLK_PERIOD} -name default

puts "\n======= 步骤 1/3: C 仿真 (5 用例, 7133 元素) ======="
csim_design -clean -O

puts "\n======= 步骤 2/3: C 综合 ======="
csynth_design

puts "\n======= 步骤 3/3: 导出 IP ======="
catch { export_design -format ip_catalog \
    -description "Systolic Array matmul (Q8.8, 4x4 PE, II=1, Tile 16x16x16)" \
    -vendor "fpga-ai" -version "1.0" } err
puts "IP export result: $err"

puts "\n======= 完成! ======="
puts "RTL: ${PROJ_NAME}/solution1/syn/verilog/ 和 .../vhdl/"
