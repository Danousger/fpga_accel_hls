# FPGA AI 加速器 — 定点数矩阵乘法

基于 **Xilinx Zynq-7020** (xc7z020clg400-**2**) 的定点数矩阵乘法硬件加速器。

## 当前状态

| 阶段 | 状态 |
|------|------|
| C 仿真 (5 用例, 7133 元素) | ✅ 全部通过, 最大误差 0.025 (6 LSB) |
| C 综合 (RTL 生成) | ✅ Verilog + VHDL |
| 时序收敛 @100MHz | ✅ 原版 N=2 (Fmax=143MHz) + 脉动阵列 (Fmax=152MHz) |
| IP 导出 | ⚠️ Vivado 2020.2 IP packager bug, 用 RTL 直接集成 (已绕过) |
| MAC 并行度实验 | ✅ N=4 可行但需降频, 净吞吐未改善 |
| **Systolic Array 架构** | ✅ **II=1, 16 MAC/cycle, Fmax=152MHz** |
| Vivado Block Design 集成 | ✅ 时序收敛 WNS=+1.24ns @100MHz, bitstream 已生成 |
| PS 端驱动 | ✅ Pynq v2.7 驱动 + Jupyter 基准测试 notebook |
| 上板验证 | 🔲 进行中 (FCLK 时钟关联已修复, 待验证) |

## 两代架构对比

| 指标 | 原版 (MAC 并行) | Systolic Array |
|------|----------------|----------------|
| 内层 II | **24** (BRAM RAW 依赖) | **1** (寄存器累加器) |
| Fmax (HLS 估计) | 143 MHz | **152 MHz** |
| DSP | 32 (14%) | **16 (7%)** |
| BRAM | 10 (6%) | **0 (0%)** |
| LUT | ~11,800 (22%) | 11,778 (22%) |
| FF | ~7,200 (6%) | 10,168 (9%) |
| 核心延迟 (单 tile) | 24,576 周期 | **705 周期** (35× 提升) |
| 有效吞吐 | 8.33M MAC/s | **理论 1.6 GMAC/s** |

## 硬件架构

### 原版 (V2): MAC 并行 + Tile 分块

```
┌──────────────────────────────────────────────────────┐
│  PL (FPGA)                                           │
│  ┌────────────────────────────────────────────────┐  │
│  │  matmul_top                                    │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────────┐ │  │
│  │  │ in_A     │  │ in_B     │  │  matmul_core │ │  │
│  │  │ (AXIS)   │  │ (AXIS)   │  │  Tile MAC    │ │  │
│  │  │    ↓     │  │    ↓     │  │      ↓       │ │  │
│  │  │ A_buf    │  │ B_buf    │  │   C_buf      │─┼──▶ out_C
│  │  │[16][16]  │  │[16][16]  │  │  [16][16]    │ │  │  (AXIS)
│  │  └──────────┘  └──────────┘  └──────────────┘ │  │
│  └────────────────────────────────────────────────┘  │
│  s_axi_control (AXI Lite) ← 矩阵维度 M, N, K        │
└──────────────────────────────────────────────────────┘
```

### Systolic Array (V4): 输出驻留 4×4 PE 阵列

```
        B[k][j] ↓   ↓   ↓   ↓
              ┌───┬───┬───┬───┐
   A[i][k] →  │PE │PE │PE │PE │  → A 流出
              ├───┼───┼───┼───┤
      →       │PE │PE │PE │PE │  →
              ├───┼───┼───┼───┤
      →       │PE │PE │PE │PE │  →
              ├───┼───┼───┼───┤
      →       │PE │PE │PE │PE │  →
              └───┴───┴───┴───┘

  每个 PE(i,j): c_reg += A × B (寄存器累加, 无 BRAM 依赖)
  输入偏斜: A 行 i 延迟 i 周期, B 列 j 延迟 j 周期
  → A[i][k] 与 B[k][j] 在周期 k+i+j 相遇于 PE(i,j)
```

## 设计参数

| 参数 | 值 | 说明 |
|------|----|------|
| 芯片 | xc7z020clg400-**2** | Zynq-7020, -2 速度等级 |
| 数据类型 | `ap_fixed<16, 8>` | Q8.8: 1符号 + 7整数 + 8小数 |
| 累加器 | `ap_fixed<48, 32>` | 48bit 防溢出 (16bit 整数) |
| Tile 尺寸 | 16×16×16 | 单 tile 占 ~1.5KB BRAM |
| 并行 MAC | 2 (原版) / 16 (脉动) | 脉动阵列 4×4 PE |
| 最大矩阵 | 64×64 | 可通过修改 `MAX_M/N/K` 增大 |
| 目标频率 | 100 MHz | -2 芯片时序收敛 |
| 接口 | AXI Stream ×3 + AXI Lite ×1 | 数据流 + 控制 |

## MAC 并行度实验 (2026-06-27)

在 -2 芯片上测试了增加 MAC 数对吞吐的影响：

| 配置 | Fmax | 内层 II | DSP | LUT | 有效吞吐 | 结论 |
|------|------|---------|-----|-----|----------|------|
| **N=2 @100MHz** | **143 MHz** ✅ | 24 | 32 (14%) | 11,792 (22%) | **8.33M MAC/s** | ← 原版基线 |
| N=4 @100MHz | 137 MHz ⚠️ | 24 | 64 (29%) | 19,462 (36%) | 16.7M (时序不过) | 资源翻倍但 slack 为负 |
| N=4 @50MHz | 78 MHz ✅ | 24 | 64 (29%) | 19,173 (36%) | 8.33M MAC/s | 时序过但吞吐未改善 |

```
有效 MAC 吞吐 = PARALLEL_N / II × 时钟频率

N=2 @100MHz:  2 / 24 × 100M = 8.33M MAC/s    ← 原版基线
N=4 @100MHz:  4 / 24 × 100M = 16.7M MAC/s    ← 时序不过
Systolic:    16 /  1 × 100M = 1600M MAC/s    ← 脉动阵列 (理论峰值)
```

**核心发现**: II=24 的累加器 RAW 依赖是吞吐瓶颈，不是 MAC 数量。单纯增加 MAC 只会恶化时序、浪费资源，不能提升实际吞吐。根本解决方案是 Systolic Array 架构 (II=1)。

## 项目结构

```
fpga-ai/
├── hls/
│   ├── src/                         # HLS 源文件 (多文件版)
│   │   ├── matmul.h                 #   类型定义 & 常量
│   │   ├── matmul_core.{h,cpp}      #   原版核心 (tile MAC, II=24)
│   │   ├── matmul_top.{h,cpp}       #   顶层 (AXI接口 + tile调度)
│   │   ├── matmul_systolic.h        #   脉动阵列声明 (SYSTOLIC_SIZE=4)
│   │   ├── matmul_systolic_core.cpp #   脉动核心 (4×4 PE, II=1)
│   │   └── matmul_systolic_top.cpp  #   脉动顶层 (接口不变, 复用 testbench)
│   ├── tb/
│   │   ├── matmul_tb.cpp            #   完整测试套件 (5 用例, 7133 元素)
│   │   └── diag_tb.cpp              #   最小诊断测试
│   ├── build/                       # 单文件综合版 (clang-tidy 兼容)
│   │   ├── matmul_all.cpp           #   原版合并源码
│   │   ├── matmul_systolic_all.cpp  #   脉动阵列合并源码 (pragma 用字面量)
│   │   ├── run.tcl / run_single.tcl #   原版构建脚本
│   │   ├── run_systolic_single.tcl  #   脉动综合 (10ns, 100MHz)
│   │   └── ...
│   ├── run_hls.tcl                  # 原版 HLS 一键脚本
│   ├── run_systolic.tcl             # 脉动阵列 csim + csynth
│   └── run_systolic_diag.tcl        # 脉动快速功能验证
├── vivado/
│   ├── build_bd.tcl                 # 原版 BD (依赖 IP packager, 已弃用)
│   └── build_bd_systolic.tcl        # 脉动阵列 BD (RTL-direct, 已验证时序)
├── ps_driver/
│   ├── pynq_driver.py               # Pynq v2.7 驱动 (3-DMA 拓扑 + benchmark)
│   ├── baremetal_driver.c           # 裸机 C 驱动 (已过时)
│   ├── diag_pl.py                   # PL 健康诊断脚本
│   └── notebooks/
│       ├── diag_pl.ipynb            #   Jupyter 诊断 (4 步 PL 检查)
│       └── bench_matmul.ipynb       #   基准测试 (4 张图 + 汇总表)
├── deploy.ps1                       # 一键部署到板子
├── AGENTS.md                        # AI agent 操作指南
└── README.md
```

## 快速开始

### 环境要求

- **Vivado 2020.2** (`E:\xilinx\Vivado\2020.2`)
- **Vitis HLS 2020.2** (`E:\xilinx\Vitis_HLS\2020.2`)
- **Git Bash** (或任意 bash shell)
- **目标板**: 领航者 ZYNQ-7020 开发板 + Pynq v2.7 镜像

### 运行 HLS C 仿真

```bash
cd /e/fpga-ai/hls
vitis_hls -f run_systolic_diag.tcl    # 脉动阵列快速验证 (单 tile)
vitis_hls -f run_systolic.tcl         # 脉动阵列完整测试 (5 用例 + 综合)
```

预期输出：
```
════ 总计: 通过 7133, 失败 0
✓ 全部通过!
```

### 运行 HLS 综合 (生成 RTL)

```bash
cd /e/fpga-ai/hls/build
vitis_hls -f run_systolic_single.tcl  # 脉动阵列综合 (10ns, 100MHz)
```

### Vivado Block Design + Bitstream

```bash
cd /e/fpga-ai/vivado
vivado -mode batch -source build_bd_systolic.tcl
```

产出:
- `vivado_proj/matmul_system.runs/impl_1/design_1_wrapper.bit`
- `vivado_proj/.../hw_handoff/design_1.hwh`
- `vivado/design_1_wrapper.bin` (FPGA manager 格式)
- 时序: WNS=+1.24ns @100MHz, 0 失败端点

### 上板部署 (Pynq v2.7)

```powershell
# 在开发机 PowerShell 里
.\deploy.ps1                              # 传 bitstream + 驱动 + 诊断
.\deploy.ps1 -IncludeNotebook             # 同时传 Jupyter notebook
```

板子上:
1. 打开 `http://<板子IP>:9090` → 运行 `diag_pl.ipynb` (PL 健康检查)
2. 诊断全过后 → 运行 `bench_matmul.ipynb` (基准测试 + 图形化)

### PS 端驱动

**Pynq (Python):**
```python
from pynq_driver import MatMulAccelerator
accel = MatMulAccelerator("design_1.bit")
C, latency_ms = accel.matmul(A_float, B_float)
results = accel.benchmark(sizes=[16, 32, 48, 64], runs=5)
```

## 流数据协议

PS 端按以下顺序通过 DMA 发送 tile 数据：

```
for m_tile in 0..M_tiles-1:
  for n_tile in 0..N_tiles-1:
    for k_tile in 0..K_tiles-1:
      DMA_A.send(A_tile[m_tile][k_tile])  # TILE_M × TILE_K 个 data_t
      DMA_B.send(B_tile[k_tile][n_tile])  # TILE_K × TILE_N 个 data_t
    DMA_C.recv(C_tile[m_tile][n_tile])    # TILE_M × TILE_N 个 data_t
```

每个 tile 固定发送/接收完整的 `TILE × TILE` 元素，边界 tile 用零填充。
脉动阵列版接口不变，testbench 和驱动直接复用。

## 已知问题

### 1. 累加器 RAW 依赖 → II=24 (原版瓶颈, 已由脉动阵列解决)

内层 `k` 循环 `C[i][j] += A×B` 存在真正的 RAW 依赖。HLS 推断 II=24。
**已解决**: Systolic Array 用寄存器累加器消除依赖, 实现 II=1。

### 2. Vivado 2020.2 IP packager bug

`export_design -format ip_catalog` 报 `bad lexical cast: core_revision` (日期戳 → int32 溢出)。
**已绕过**: `build_bd_systolic.tcl` 用 `create_bd_cell -type module -reference` 直接例化 RTL。

### 3. Vitis HLS 2020.2 clang-tidy 限制

- 不解析 `#include "..."` → 单文件综合 (`matmul_systolic_all.cpp`)
- 不展开 `#pragma` 宏 → pragma 用字面量 (`factor=4` 而非 `factor=SYSTOLIC_SIZE`)

### 4. Pynq v2.7 FCLK 时钟使能 (已修复)

Pynq `Overlay.download()` 需要从 .hwh 读到 AXI 接口→时钟关联才会使能 FCLK0。
HLS RTL 模块的 `ap_clk` 不会自动关联 AXI 接口 → .hwh 缺少时钟依赖 → PL 无时钟 → 挂死。
**已修复**: `build_bd_systolic.tcl` 加 `ASSOCIATED_BUSIF {s_axi_control:in_A_V:in_B_V:out_C_V}` 到 `ap_clk`。

## 优化路线图

- [x] **V1**: 单 MAC 单元, 功能验证通过
- [x] **V2**: 2 MAC 并行 + Tile 分块 + AXI Stream 接口 (Fmax=143 MHz)
- [x] **V2.5**: MAC 并行度实验 — 验证了 N=4 不提升净吞吐, 确认 II=24 为根本瓶颈
- [x] **V3**: Vivado Block Design 集成 (RTL-direct, 时序收敛 WNS=+1.24ns)
- [x] **V4**: Systolic Array 架构 — 消除累加器 RAW 依赖, 实现 II=1, 16 MAC/cycle
- [ ] **V4.1**: 上板验证 — FCLK 修复已部署, 待最终测试
- [ ] **V5**: 4~8 MAC Systolic + Ping-Pong 双缓冲
- [ ] **V6**: AXI Master 直读 DDR, 支持大矩阵 (>64×64)

### 技术债务

| 项目 | 描述 |
|------|------|
| `run_single.tcl` 时钟 | 原版为 20ns (50MHz), 生产 N=2 需改回 10ns |
| `matmul_all.cpp` PARALLEL_N | 原版为 4 (实验残留), 生产需改回 2 |
| `baremetal_driver.c` | 已过时 (错误 DMA 拓扑 + 寄存器偏移), 用 `pynq_driver.py` |
