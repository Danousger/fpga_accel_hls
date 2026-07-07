# ============================================================================
# build_bd_systolic.tcl — Systolic Array 版 Block Design + 完整 Vivado 流程
#
# 用法:
#   cd /e/fpga-ai/vivado
#   vivado -mode batch -source build_bd_systolic.tcl
#
# 与原 build_bd.tcl 的区别:
#   1. 直接例化 HLS RTL (绕开 IP packager bug)
#   2. 手动配 PS7 (领航者无板级预设, 用 generic DDR3)
#   3. 3 个 DMA 共享 1 个 HP 口 (节省 PS 资源)
#   4. 跑到 write_bitstream, 验证时序收敛
#
# 前置条件: 已运行 hls/build/run_systolic_single.tcl 生成 RTL
#   RTL 路径: ../hls/build/systolic_single/solution1/syn/verilog/
# ============================================================================

set PROJ_NAME "matmul_system"
set PROJ_DIR  "./vivado_proj"
set HLS_RTL_DIR "../hls/build/systolic_single/solution1/syn/verilog"

# ---------------------------------------------------------------------------
# 1. 创建工程 + 添加 HLS RTL
# ---------------------------------------------------------------------------
if {[file exists ${PROJ_DIR}]} {
    file delete -force ${PROJ_DIR}
}
create_project ${PROJ_NAME} ${PROJ_DIR} -part xc7z020clg400-2

puts "\[INFO\] 添加 HLS RTL: ${HLS_RTL_DIR}"
add_files -norecurse [glob ${HLS_RTL_DIR}/*.v]
update_compile_order -fileset sources_1

# ---------------------------------------------------------------------------
# 2. 创建 Block Design
# ---------------------------------------------------------------------------
create_bd_design "design_1"

# ============================================================================
# 2.1 Zynq7 Processing System (手动配置, 无板级预设)
# ============================================================================
set zynq [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 \
          processing_system7_0]

# ---------------------------------------------------------------------------
# PS7 全部配置 — 单个 set_property 调用, 避免多次调用间参数依赖重置
# 参数照搬领航者板 base overlay (已验证可工作), 含 DDR3 + MIO + FCLK
#
# ★ 关键: FCLK_CLK0 时钟链必须完整配置, 否则 PL 侧无时钟 → AXI 无响应 → 挂死
#   IO PLL (1000MHz) / DIVISOR0=5 / DIVISOR1=2 = 100MHz, BUF=TRUE 使能缓冲器
# ---------------------------------------------------------------------------

# DDR3: SK Hynix H5TC4G63EFR-RDA (2片 × 4Gb ×16 = 1GB @ 32-bit)
# PARTNO=MT41J256M16 RE-125 (注意 J 不是 K), SPEED_BIN=DDR3_1066F
# tRCD/tRP 单位是 cycles (非 ns), 值=7
set_property -dict [list \
    CONFIG.PCW_UIPARAM_DDR_MEMORY_TYPE {DDR 3} \
    CONFIG.PCW_UIPARAM_DDR_PARTNO {MT41J256M16 RE-125} \
    CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH {32 Bit} \
    CONFIG.PCW_UIPARAM_DDR_SPEED_BIN {DDR3_1066F} \
    CONFIG.PCW_UIPARAM_DDR_DEVICE_CAPACITY {4096 MBits} \
    CONFIG.PCW_UIPARAM_DDR_DRAM_WIDTH {16 Bits} \
    CONFIG.PCW_UIPARAM_DDR_CL {7} \
    CONFIG.PCW_UIPARAM_DDR_CWL {6} \
    CONFIG.PCW_UIPARAM_DDR_T_FAW {40.0} \
    CONFIG.PCW_UIPARAM_DDR_T_RAS_MIN {35.0} \
    CONFIG.PCW_UIPARAM_DDR_T_RC {48.91} \
    CONFIG.PCW_UIPARAM_DDR_T_RCD {7} \
    CONFIG.PCW_UIPARAM_DDR_T_RP {7} \
    CONFIG.PCW_UIPARAM_DDR_TRAIN_DATA_EYE {1} \
    CONFIG.PCW_UIPARAM_DDR_TRAIN_READ_GATE {1} \
    CONFIG.PCW_UIPARAM_DDR_TRAIN_WRITE_LEVEL {1} \
    CONFIG.PCW_UIPARAM_DDR_USE_INTERNAL_VREF {0} \
    CONFIG.PCW_UIPARAM_DDR_BANK_ADDR_COUNT {3} \
    CONFIG.PCW_UIPARAM_DDR_ROW_ADDR_COUNT {15} \
    CONFIG.PCW_UIPARAM_DDR_COL_ADDR_COUNT {10} \
    CONFIG.PCW_UIPARAM_DDR_BL {8} \
    CONFIG.PCW_UIPARAM_DDR_AL {0} \
    CONFIG.PCW_UIPARAM_DDR_ECC {Disabled} \
    CONFIG.PCW_UIPARAM_DDR_CLOCK_0_PACKAGE_LENGTH {80.4535} \
    CONFIG.PCW_UIPARAM_DDR_CLOCK_1_PACKAGE_LENGTH {80.4535} \
    CONFIG.PCW_UIPARAM_DDR_CLOCK_2_PACKAGE_LENGTH {80.4535} \
    CONFIG.PCW_UIPARAM_DDR_CLOCK_3_PACKAGE_LENGTH {80.4535} \
    CONFIG.PCW_UIPARAM_DDR_DQS_0_PACKAGE_LENGTH {105.056} \
    CONFIG.PCW_UIPARAM_DDR_DQS_1_PACKAGE_LENGTH {66.904} \
    CONFIG.PCW_UIPARAM_DDR_DQS_2_PACKAGE_LENGTH {89.1715} \
    CONFIG.PCW_UIPARAM_DDR_DQS_3_PACKAGE_LENGTH {113.63} \
    CONFIG.PCW_UIPARAM_DDR_DQ_0_PACKAGE_LENGTH {98.503} \
    CONFIG.PCW_UIPARAM_DDR_DQ_1_PACKAGE_LENGTH {68.5855} \
    CONFIG.PCW_UIPARAM_DDR_DQ_2_PACKAGE_LENGTH {90.295} \
    CONFIG.PCW_UIPARAM_DDR_DQ_3_PACKAGE_LENGTH {103.977} \
    CONFIG.PCW_PACKAGE_DDR_BOARD_DELAY0 {0.089} \
    CONFIG.PCW_PACKAGE_DDR_BOARD_DELAY1 {0.075} \
    CONFIG.PCW_PACKAGE_DDR_BOARD_DELAY2 {0.085} \
    CONFIG.PCW_PACKAGE_DDR_BOARD_DELAY3 {0.092} \
    CONFIG.PCW_PACKAGE_DDR_DQS_TO_CLK_DELAY_0 {-0.025} \
    CONFIG.PCW_PACKAGE_DDR_DQS_TO_CLK_DELAY_1 {0.014} \
    CONFIG.PCW_PACKAGE_DDR_DQS_TO_CLK_DELAY_2 {-0.009} \
    CONFIG.PCW_PACKAGE_DDR_DQS_TO_CLK_DELAY_3 {-0.033} \
    CONFIG.PCW_PRESET_BANK0_VOLTAGE {LVCMOS 3.3V} \
    CONFIG.PCW_PRESET_BANK1_VOLTAGE {LVCMOS 1.8V} \
    CONFIG.PCW_CRYSTAL_PERIPHERAL_FREQMHZ {33.333333} \
    CONFIG.PCW_UART0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_UART0_UART0_IO {MIO 14 .. 15} \
    CONFIG.PCW_UART0_BAUD_RATE {115200} \
    CONFIG.PCW_UART1_PERIPHERAL_ENABLE {0} \
    CONFIG.PCW_SD0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_SD0_SD0_IO {MIO 40 .. 45} \
    CONFIG.PCW_SD0_GRP_CD_ENABLE {1} \
    CONFIG.PCW_SD0_GRP_CD_IO {MIO 10} \
    CONFIG.PCW_SD1_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_SD1_SD1_IO {MIO 46 .. 51} \
    CONFIG.PCW_ENET0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_ENET0_ENET0_IO {MIO 16 .. 27} \
    CONFIG.PCW_ENET0_GRP_MDIO_ENABLE {1} \
    CONFIG.PCW_ENET0_GRP_MDIO_IO {MIO 52 .. 53} \
    CONFIG.PCW_ENET0_PERIPHERAL_FREQMHZ {1000 Mbps} \
    CONFIG.PCW_ENET1_PERIPHERAL_ENABLE {0} \
    CONFIG.PCW_USB0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_USB0_USB0_IO {MIO 28 .. 39} \
    CONFIG.PCW_USB0_RESET_ENABLE {1} \
    CONFIG.PCW_USB0_RESET_IO {MIO 9} \
    CONFIG.PCW_USB_RESET_POLARITY {Active Low} \
    CONFIG.PCW_QSPI_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_QSPI_QSPI_IO {MIO 1 .. 6} \
    CONFIG.PCW_QSPI_GRP_SINGLE_SS_ENABLE {1} \
    CONFIG.PCW_QSPI_GRP_SINGLE_SS_IO {MIO 1 .. 6} \
    CONFIG.PCW_SINGLE_QSPI_DATA_MODE {x4} \
    CONFIG.PCW_QSPI_PERIPHERAL_FREQMHZ {200} \
    CONFIG.PCW_GPIO_MIO_GPIO_ENABLE {1} \
    CONFIG.PCW_GPIO_MIO_GPIO_IO {MIO} \
    CONFIG.PCW_EN_CLK0_PORT {1} \
    CONFIG.PCW_EN_RST0_PORT {1} \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {100} \
    CONFIG.PCW_FCLK0_PERIPHERAL_CLKSRC {IO PLL} \
    CONFIG.PCW_FCLK0_PERIPHERAL_DIVISOR0 {5} \
    CONFIG.PCW_FCLK0_PERIPHERAL_DIVISOR1 {2} \
    CONFIG.PCW_FCLK_CLK0_BUF {TRUE} \
    CONFIG.PCW_USE_M_AXI_GP0 {1} \
    CONFIG.PCW_USE_S_AXI_HP0 {1} \
] [get_bd_cells processing_system7_0]

# 外部引脚 (DDR + 固定 IO)
make_bd_intf_pins_external  [get_bd_intf_pins processing_system7_0/DDR]
make_bd_intf_pins_external  [get_bd_intf_pins processing_system7_0/FIXED_IO]

puts "\[INFO\] PS7 配置完成 (DDR3 + MIO + FCLK 时钟链, 照搬 base overlay)"

# ============================================================================
# 2.2 AXI DMA (3 个单向 DMA: A、B 发送, C 接收)
# ============================================================================
# DMA A: MM2S — PS DDR → 加速器 in_A
set dma_A [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 axi_dma_A]
set_property -dict [list \
    CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_s2mm {0} \
    CONFIG.c_include_sg {0} \
    CONFIG.c_m_axi_mm2s_data_width {32} \
    CONFIG.c_m_axis_mm2s_tdata_width {16} \
] $dma_A

# DMA B: MM2S — PS DDR → 加速器 in_B
set dma_B [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 axi_dma_B]
set_property -dict [list \
    CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_s2mm {0} \
    CONFIG.c_include_sg {0} \
    CONFIG.c_m_axi_mm2s_data_width {32} \
    CONFIG.c_m_axis_mm2s_tdata_width {16} \
] $dma_B

# DMA C: S2MM — 加速器 out_C → PS DDR
# 注: S2MM 侧的 axis 宽度参数是 c_s_axis_s2mm_tdata_width (非 c_m_axis)
set dma_C [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 axi_dma_C]
set_property -dict [list \
    CONFIG.c_include_mm2s {0} \
    CONFIG.c_include_s2mm {1} \
    CONFIG.c_include_sg {0} \
    CONFIG.c_m_axi_s2mm_data_width {32} \
    CONFIG.c_s_axis_s2mm_tdata_width {16} \
] $dma_C

puts "\[INFO\] 3 个 DMA 配置完成"

# ============================================================================
# 2.3 矩阵乘法加速器 (直接例化 RTL 模块, 绕开 IP packager)
# ============================================================================
set matmul [create_bd_cell -type module -reference matmul_top matmul_top_0]

puts "\[INFO\] matmul_top RTL 模块例化完成"

# ============================================================================
# 2.4 AXI Interconnect (PS GP0 → 4 个 AXI Lite 从口)
# ============================================================================
set axi_ic [create_bd_cell -type ip -vlnv \
    xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_0]
set_property -dict [list \
    CONFIG.NUM_MI {4} \
    CONFIG.NUM_SI {1} \
] $axi_ic

# ============================================================================
# 2.5 SmartConnect (3 个 DMA M_AXI → PS HP0)
# ============================================================================
set smart_connect [create_bd_cell -type ip -vlnv \
    xilinx.com:ip:smartconnect:1.0 smartconnect_0]
set_property -dict [list \
    CONFIG.NUM_SI {3} \
    CONFIG.NUM_MI {1} \
] $smart_connect

# ============================================================================
# 2.6 Processor System Reset (同步复位)
# ============================================================================
set rst_ps [create_bd_cell -type ip -vlnv \
    xilinx.com:ip:proc_sys_reset:5.0 rst_ps7_0]

# ============================================================================
# 3. 连线
# ============================================================================

# --- 时钟: FCLK_CLK0 (100MHz) 驱动所有 PL 时钟 ---
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins processing_system7_0/M_AXI_GP0_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins processing_system7_0/S_AXI_HP0_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins rst_ps7_0/slowest_sync_clk]
# DMA 时钟: M_AXI (memory-mapped) + S_AXI_LITE (寄存器) 都用 FCLK_CLK0
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_A/m_axi_mm2s_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_A/s_axi_lite_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_B/m_axi_mm2s_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_B/s_axi_lite_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_C/m_axi_s2mm_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_C/s_axi_lite_aclk]
# AXI Interconnect: 主口 + 所有从口时钟
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_interconnect_0/ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_interconnect_0/S00_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_interconnect_0/M00_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_interconnect_0/M01_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_interconnect_0/M02_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_interconnect_0/M03_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins smartconnect_0/aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins matmul_top_0/ap_clk]

# --- 关键: 关联 matmul_top 的 AXI 总线接口到 ap_clk ---
# 没有这个, .hwh 不会记录时钟依赖, Pynq download() 不会使能 FCLK0 → PL 无时钟 → 挂死
# ASSOCIATED_BUSIF 列出所有由 ap_clk 驱动的 AXI 总线接口名
set_property CONFIG.ASSOCIATED_BUSIF {s_axi_control:in_A_V:in_B_V:out_C_V} \
    [get_bd_pins matmul_top_0/ap_clk]

# --- 复位 ---
connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] \
               [get_bd_pins rst_ps7_0/ext_reset_in]
connect_bd_net [get_bd_pins rst_ps7_0/peripheral_aresetn] \
               [get_bd_pins axi_dma_A/axi_resetn]
connect_bd_net [get_bd_pins rst_ps7_0/peripheral_aresetn] \
               [get_bd_pins axi_dma_B/axi_resetn]
connect_bd_net [get_bd_pins rst_ps7_0/peripheral_aresetn] \
               [get_bd_pins axi_dma_C/axi_resetn]
connect_bd_net [get_bd_pins rst_ps7_0/peripheral_aresetn] \
               [get_bd_pins axi_interconnect_0/ARESETN]
connect_bd_net [get_bd_pins rst_ps7_0/peripheral_aresetn] \
               [get_bd_pins smartconnect_0/aresetn]
connect_bd_net [get_bd_pins rst_ps7_0/peripheral_aresetn] \
               [get_bd_pins matmul_top_0/ap_rst_n]

# --- AXI Lite 控制通路: PS GP0 → Interconnect → 4 个从口 ---
connect_bd_intf_net [get_bd_intf_pins processing_system7_0/M_AXI_GP0] \
                     [get_bd_intf_pins axi_interconnect_0/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_interconnect_0/M00_AXI] \
                     [get_bd_intf_pins matmul_top_0/s_axi_control]
connect_bd_intf_net [get_bd_intf_pins axi_interconnect_0/M01_AXI] \
                     [get_bd_intf_pins axi_dma_A/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins axi_interconnect_0/M02_AXI] \
                     [get_bd_intf_pins axi_dma_B/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins axi_interconnect_0/M03_AXI] \
                     [get_bd_intf_pins axi_dma_C/S_AXI_LITE]

# --- AXI Stream 数据通路 ---
# 注: HLS RTL 的 AXIS 接口名带 _V 后缀 (in_A_V, in_B_V, out_C_V)
# DMA_A → matmul_top.in_A_V
connect_bd_intf_net [get_bd_intf_pins axi_dma_A/M_AXIS_MM2S] \
                     [get_bd_intf_pins matmul_top_0/in_A_V]
# DMA_B → matmul_top.in_B_V
connect_bd_intf_net [get_bd_intf_pins axi_dma_B/M_AXIS_MM2S] \
                     [get_bd_intf_pins matmul_top_0/in_B_V]
# matmul_top.out_C_V → DMA_C
connect_bd_intf_net [get_bd_intf_pins matmul_top_0/out_C_V] \
                     [get_bd_intf_pins axi_dma_C/S_AXIS_S2MM]

# --- AXI Memory-Mapped: 3 个 DMA M_AXI → SmartConnect → HP0 ---
connect_bd_intf_net [get_bd_intf_pins axi_dma_A/M_AXI_MM2S] \
                     [get_bd_intf_pins smartconnect_0/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_B/M_AXI_MM2S] \
                     [get_bd_intf_pins smartconnect_0/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_C/M_AXI_S2MM] \
                     [get_bd_intf_pins smartconnect_0/S02_AXI]
connect_bd_intf_net [get_bd_intf_pins smartconnect_0/M00_AXI] \
                     [get_bd_intf_pins processing_system7_0/S_AXI_HP0]

# ============================================================================
# 4. 地址分配 & 验证
# ============================================================================
assign_bd_address
validate_bd_design

puts "\[INFO\] Block Design 验证通过"

# ============================================================================
# 5. 生成 HDL Wrapper
# ============================================================================
make_wrapper -files [get_files ${PROJ_DIR}/${PROJ_NAME}.srcs/sources_1/bd/design_1/design_1.bd] -top
add_files -norecurse ${PROJ_DIR}/${PROJ_NAME}.gen/sources_1/bd/design_1/hdl/design_1_wrapper.v
set_property top design_1_wrapper [current_fileset]
update_compile_order -fileset sources_1

puts "\n========================================"
puts " Block Design 构建完成, 开始实现流程"
puts "========================================\n"

# ============================================================================
# 6. 综合 + 实现 + 生成 Bitstream
# ============================================================================
# 先跑综合
launch_runs synth_1 -jobs 4
wait_on_run synth_1

set synth_status [get_property STATUS [get_runs synth_1]]
puts "\[INFO\] 综合状态: ${synth_status}"

# 跑实现 + bitstream
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1

set impl_status [get_property STATUS [get_runs impl_1]]
puts "\[INFO\] 实现状态: ${impl_status}"

# ============================================================================
# 7. 时序报告
# ============================================================================
if {[file exists ${PROJ_DIR}/${PROJ_NAME}.runs/impl_1/design_1_wrapper_timing_summary_routed.rpt]} {
    puts "\n========================================"
    puts " 时序报告摘要"
    puts "========================================"
    # 用 catch 避免报告格式问题导致脚本失败
    catch {
        open_run impl_1
        report_timing_summary -datasheet -file timing_summary.rpt
        close_design
    }

    # 生成 .bin 文件 (Zynq FPGA manager 需要 byte-swapped .bin)
    # -bin_file 选项额外生成 .bin (与 .bit 同名), 文件名必须用 .bit 扩展名
    open_run impl_1
    write_bitstream -bin_file -force design_1_wrapper.bit
    puts "\[INFO\] .bin 文件已生成: [pwd]/design_1_wrapper.bin"
    close_design

    puts "完整时序报告: ${PROJ_DIR}/${PROJ_NAME}.runs/impl_1/design_1_wrapper_timing_summary_routed.rpt"
} else {
    puts "\[WARN\] 时序报告未生成, 实现可能失败"
}

# ============================================================================
# 8. 结果汇总
# ============================================================================
puts "\n========================================"
puts " 流程完成!"
puts "========================================"
puts "工程目录: ${PROJ_DIR}"
puts "Bitstream: ${PROJ_DIR}/${PROJ_NAME}.runs/impl_1/design_1_wrapper.bit"
puts "Bin文件: [pwd]/design_1_wrapper.bin"
puts "时序报告: ${PROJ_DIR}/${PROJ_NAME}.runs/impl_1/design_1_wrapper_timing_summary_routed.rpt"
puts "========================================\n"
