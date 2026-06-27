# ============================================================================
# directives.tcl — HLS 优化指令
#
# 此文件定义综合策略和性能目标。
# 在 Vitis HLS 中通过 Solution → Solution Settings → Add Directive 加载,
# 或直接在 run_hls.tcl 中使用.
# ============================================================================

# ---------------------------------------------------------------------------
# 顶层函数: matmul_top
# ---------------------------------------------------------------------------

# AXI Stream 接口深度 (FIFO 缓冲大小)
# scale_factor=1 → 使用默认深度 (通常为 1~2)
# 增大深度可以提高吞吐但消耗更多 FF/LUT
set_directive_interface -mode axis -register -register_mode both matmul_top in_A
set_directive_interface -mode axis -register -register_mode both matmul_top in_B
set_directive_interface -mode axis -register -register_mode both matmul_top out_C

# AXI Lite 控制接口
set_directive_interface -mode s_axilite matmul_top M
set_directive_interface -mode s_axilite matmul_top N
set_directive_interface -mode s_axilite matmul_top K

# ---------------------------------------------------------------------------
# 核心函数: matmul_core
# ---------------------------------------------------------------------------

# 内层循环流水线 — 目标 II=1 (每个周期发射一轮 MAC)
# 如果时序不满足, 可以放宽到 II=2
set_directive_pipeline -II 1 matmul_core/k_loop

# ---------------------------------------------------------------------------
# 数组分区 (已在代码中通过 ARRAY_PARTITION pragma 处理)
# 这里作为补充 — 如果代码内 pragma 被忽略时使用
# ---------------------------------------------------------------------------

# A_buf: dim=2 (K 维度) cyclic partition, factor=2
# → 每周期可从同一行 A 读取 2 个不同元素
# set_directive_array_partition -type cyclic -factor 2 -dim 2 matmul_core A_buf

# B_buf: dim=2 (N 维度) cyclic partition, factor=PARALLEL_N
# → 每周期可从同一行 B 读取 PARALLEL_N 个元素
# set_directive_array_partition -type cyclic -factor 4 -dim 2 matmul_core B_buf

# C_buf: dim=2 (N 维度) cyclic partition, factor=PARALLEL_N
# → 每周期可写入 PARALLEL_N 个部分和
# set_directive_array_partition -type cyclic -factor 4 -dim 2 matmul_core C_buf

# ---------------------------------------------------------------------------
# 性能目标
# ---------------------------------------------------------------------------
# Zynq-7020 典型时钟: 100MHz (周期 10ns)
set_clock_uncertainty 1.5
