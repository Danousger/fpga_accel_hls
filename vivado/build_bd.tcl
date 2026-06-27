# ============================================================================
# build_bd.tcl — Vivado Block Design 自动化构建脚本
#
# 前置条件:
#   1. 已在 Vitis HLS 中导出 matmul_accel IP (Vivado IP Catalog 格式)
#   2. 将 IP 仓库路径添加到 Vivado
#
# 用法:
#   vivado -mode batch -source build_bd.tcl
#
# 生成的 Block Design 架构:
#
#   ┌─────────────────────┐
#   │  Zynq7 Processing   │
#   │  System (PS)        │
#   │  M_AXI_GP0 ────────►│──► AXI Interconnect ──► matmul_top (s_axi_control)
#   │  S_AXI_HP0 ◄────────│◄── AXI DMA (MM2S) ────► matmul_top (in_A)
#   │  S_AXI_HP1 ◄────────│◄── AXI DMA (MM2S) ────► matmul_top (in_B)
#   │  S_AXI_HP2 ◄────────│◄── AXI DMA (S2MM) ◄──── matmul_top (out_C)
#   └─────────────────────┘
#
# ============================================================================

# ---------------------------------------------------------------------------
# 项目创建
# ---------------------------------------------------------------------------
set PROJ_NAME "matmul_system"
set PROJ_DIR  "./vivado_proj"

if {[file exists ${PROJ_DIR}]} {
    file delete -force ${PROJ_DIR}
}
create_project ${PROJ_NAME} ${PROJ_DIR} -part xc7z020clg400-2

# ---------------------------------------------------------------------------
# 添加 HLS IP 仓库
#   (指向 HLS 导出的 IP 目录, 根据实际路径修改)
# ---------------------------------------------------------------------------
set HLS_IP_REPO "../hls/proj/matmul_accel/solution1/impl/ip"
if {[file exists ${HLS_IP_REPO}]} {
    set_property ip_repo_paths ${HLS_IP_REPO} [current_project]
    update_ip_catalog
    puts "\[INFO\] 已添加 HLS IP 仓库: ${HLS_IP_REPO}"
} else {
    puts "\[WARN\] HLS IP 仓库未找到: ${HLS_IP_REPO}"
    puts "\[WARN\] 请先运行 Vitis HLS 导出 IP, 然后修改此路径"
}

# ---------------------------------------------------------------------------
# 创建 Block Design
# ---------------------------------------------------------------------------
create_bd_design "design_1"

# ============================================================================
# 1. 添加并配置 Zynq-7 Processing System
# ============================================================================
set zynq [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 \
          processing_system7_0]

# 应用 board preset (如果有) 或使用默认配置
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" Master "Disable" Slave "Disable"}

# 配置 PS-PL 接口:
#   - 使能 AXI HP0, HP1, HP2 (高带宽从口, PL 可访问 DDR)
#   - 使能 AXI GP0 (主口, PS 配置 PL 寄存器)
set_property -dict [list \
    CONFIG.PCW_USE_S_AXI_HP0 {1} \
    CONFIG.PCW_USE_S_AXI_HP1 {1} \
    CONFIG.PCW_USE_S_AXI_HP2 {1} \
    CONFIG.PCW_USE_M_AXI_GP0 {1} \
    CONFIG.PCW_USE_M_AXI_GP1 {0} \
] [get_bd_cells processing_system7_0]

# ---------------------------------------------------------------------------
# 2. 添加 AXI DMA (用于加速器数据流)
# ---------------------------------------------------------------------------

# DMA 1: MM2S — PS DDR → 加速器 A 矩阵
set dma_A [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 axi_dma_A]
set_property -dict [list \
    CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_s2mm {0} \
    CONFIG.c_include_sg {0} \
    CONFIG.c_m_axi_mm2s_data_width {32} \
    CONFIG.c_m_axis_mm2s_tdata_width {16} \
] $dma_A

# DMA 2: MM2S — PS DDR → 加速器 B 矩阵
set dma_B [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 axi_dma_B]
set_property -dict [list \
    CONFIG.c_include_mm2s {1} \
    CONFIG.c_include_s2mm {0} \
    CONFIG.c_include_sg {0} \
    CONFIG.c_m_axi_mm2s_data_width {32} \
    CONFIG.c_m_axis_mm2s_tdata_width {16} \
] $dma_B

# DMA 3: S2MM — 加速器 C 矩阵 → PS DDR
set dma_C [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 axi_dma_C]
set_property -dict [list \
    CONFIG.c_include_mm2s {0} \
    CONFIG.c_include_s2mm {1} \
    CONFIG.c_include_sg {0} \
    CONFIG.c_m_axi_s2mm_data_width {32} \
    CONFIG.c_m_axis_s2mm_tdata_width {32} \
] $dma_C

# ---------------------------------------------------------------------------
# 3. 添加矩阵乘法加速器 IP (HLS)
# ---------------------------------------------------------------------------
set matmul [create_bd_cell -type ip -vlnv \
    fpga-ai:matmul_accel:matmul_top:1.0 matmul_top_0]

# ---------------------------------------------------------------------------
# 4. 添加 AXI Interconnect (PS GP0 → 各 IP 的 s_axi)
# ---------------------------------------------------------------------------
set axi_ic [create_bd_cell -type ip -vlnv \
    xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_0]
set_property -dict [list \
    CONFIG.NUM_MI {4} \
    CONFIG.NUM_SI {1} \
] $axi_ic

# ---------------------------------------------------------------------------
# 5. 添加 AXI SmartConnect (DMA M_AXI → PS HP 口)
# ---------------------------------------------------------------------------
set smart_connect [create_bd_cell -type ip -vlnv \
    xilinx.com:ip:smartconnect:1.0 smartconnect_0]
set_property -dict [list \
    CONFIG.NUM_SI {3} \
    CONFIG.NUM_MI {1} \
] $smart_connect

# ---------------------------------------------------------------------------
# 6. 添加 Processor System Reset (同步复位)
# ---------------------------------------------------------------------------
set rst_ps [create_bd_cell -type ip -vlnv \
    xilinx.com:ip:proc_sys_reset:5.0 rst_ps7_0]

# ============================================================================
# 连接 (手动完成关键连接, 其余用 Automation)
# ============================================================================

# --- 时钟 ---
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins processing_system7_0/M_AXI_GP0_ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins rst_ps7_0/slowest_sync_clk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_A/m_axi_mm2s_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_B/m_axi_mm2s_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_dma_C/m_axi_s2mm_aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins axi_interconnect_0/ACLK]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins smartconnect_0/aclk]
connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] \
               [get_bd_pins matmul_top_0/ap_clk]

# --- 复位 ---
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

connect_bd_net [get_bd_pins processing_system7_0/FCLK_RESET0_N] \
               [get_bd_pins rst_ps7_0/ext_reset_in]

# --- AXI Lite 控制通路 ---
# PS GP0 → Interconnect → matmul_top (s_axi_control)
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

# --- AXI Stream 数据通路 (加速器) ---
# DMA_A → matmul_top (in_A)
connect_bd_intf_net [get_bd_intf_pins axi_dma_A/M_AXIS_MM2S] \
                    [get_bd_intf_pins matmul_top_0/in_A]

# DMA_B → matmul_top (in_B)
connect_bd_intf_net [get_bd_intf_pins axi_dma_B/M_AXIS_MM2S] \
                    [get_bd_intf_pins matmul_top_0/in_B]

# matmul_top (out_C) → DMA_C
connect_bd_intf_net [get_bd_intf_pins matmul_top_0/out_C] \
                    [get_bd_intf_pins axi_dma_C/S_AXIS_S2MM]

# --- AXI Memory-Mapped 数据通路 (DMA → PS HP) ---
# 所有 DMA 的 M_AXI 通过 SmartConnect 共享 HP0
connect_bd_intf_net [get_bd_intf_pins axi_dma_A/M_AXI_MM2S] \
                    [get_bd_intf_pins smartconnect_0/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_B/M_AXI_MM2S] \
                    [get_bd_intf_pins smartconnect_0/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_dma_C/M_AXI_S2MM] \
                    [get_bd_intf_pins smartconnect_0/S02_AXI]
connect_bd_intf_net [get_bd_intf_pins smartconnect_0/M00_AXI] \
                    [get_bd_intf_pins processing_system7_0/S_AXI_HP0]

# ============================================================================
# 地址分配 & 验证
# ============================================================================
assign_bd_address
validate_bd_design

# ============================================================================
# 生成 HDL Wrapper & 综合
# ============================================================================
make_wrapper -files [get_files ${PROJ_DIR}/${PROJ_NAME}.srcs/sources_1/bd/design_1/design_1.bd] -top
add_files -norecurse ${PROJ_DIR}/${PROJ_NAME}.gen/sources_1/bd/design_1/hdl/design_1_wrapper.v
set_property top design_1_wrapper [current_fileset]
update_compile_order -fileset sources_1

puts "\n========================================"
puts " Block Design 构建完成!"
puts " 接下来: Generate Bitstream"
puts "========================================\n"
