# FPGA AI 加速器 — 定点数矩阵乘法

基于 **Xilinx Zynq-7020** (xc7z020clg400-**2**) 的定点数矩阵乘法硬件加速器。

## 当前状态

| 阶段 | 状态 |
|------|------|
| C 仿真 (5 用例, 7133 元素) | ✅ 全部通过, 最大误差 0.025 (6 LSB) |
| C 综合 (RTL 生成) | ✅ Verilog + VHDL |
| 时序收敛 @100MHz | ✅ N=2 收敛 (Fmax=143 MHz, -2 芯片) |
| IP 导出 | ⚠️ Vivado 2020.2 IP packager bug |
| MAC 并行度实验 | ✅ N=4 可行但需降频, 净吞吐未改善 |
| Vivado Block Design 集成 | 🔲 待完成 |
| 上板验证 | 🔲 待完成 |

## 硬件架构

```
┌──────────────────────────────────────────────────────┐
│  PS (ARM Cortex-A9)                                  │
│  应用程序 → 矩阵分块 & 调度 → DMA 控制器              │
│                      │ AXI HP                        │
├──────────────────────┼───────────────────────────────┤
│  PL (FPGA)           ▼                               │
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

## 设计参数

| 参数 | 值 | 说明 |
|------|----|------|
| 芯片 | xc7z020clg400-**2** | Zynq-7020, -2 速度等级 |
| 数据类型 | `ap_fixed<16, 8>` | Q8.8: 1符号 + 7整数 + 8小数 |
| 累加器 | `ap_fixed<48, 32>` | 48bit 防溢出 (16bit 整数) |
| Tile 尺寸 | 16×16×16 | 单 tile 占 ~1.5KB BRAM |
| 并行 MAC | 2 | 当前基线 (见下方实验) |
| 最大矩阵 | 64×64 | 可通过修改 `MAX_M/N/K` 增大 |
| 目标频率 | 100 MHz | -2 芯片时序收敛 |
| 接口 | AXI Stream ×3 + AXI Lite ×1 | 数据流 + 控制 |

### 资源占用 (PARALLEL_N=2, xc7z020clg400-2, 100MHz)

| 资源 | 用量 | 占比 |
|------|------|------|
| DSP48E | 32 | 14% |
| BRAM | 10 | 6% |
| LUT | ~11,800 | 22% |
| FF | ~7,200 | 6% |

## MAC 并行度实验 (2026-06-27)

在 -2 芯片上测试了增加 MAC 数对吞吐的影响：

| 配置 | Fmax | 内层 II | DSP | LUT | 有效吞吐 | 结论 |
|------|------|---------|-----|-----|----------|------|
| **N=2 @100MHz** | **143 MHz** ✅ | 24 | 32 (14%) | 11,792 (22%) | **8.33M MAC/s** | ← 当前基线 |
| N=4 @100MHz | 137 MHz ⚠️ | 24 | 64 (29%) | 19,462 (36%) | 16.7M (时序不过) | 资源翻倍但 slack 为负 |
| N=4 @50MHz | 78 MHz ✅ | 24 | 64 (29%) | 19,173 (36%) | 8.33M MAC/s | 时序过但吞吐未改善 |

```
有效 MAC 吞吐 = PARALLEL_N / II × 时钟频率

N=2 @100MHz:  2 / 24 × 100M = 8.33M MAC/s   ← 基线
N=4 @100MHz:  4 / 24 × 100M = 16.7M MAC/s   ← 时序不过
N=4 @50MHz:   4 / 24 × 50M  = 8.33M MAC/s   ← 无收益
N=8 @100MHz:  8 / 24 × 100M = 33.3M MAC/s   ← 理论值, II=1 的目标
```

**核心发现**: II=24 的累加器 RAW 依赖是吞吐瓶颈，不是 MAC 数量。单纯增加 MAC 只会恶化时序、浪费资源，不能提升实际吞吐。根本解决方案是 Systolic Array 架构 (II=1)。

## 项目结构

```
fpga-ai/
├── hls/
│   ├── src/                    # HLS 源文件
│   │   ├── matmul.h            #   类型定义 & 常量
│   │   ├── matmul_core.{h,cpp} #   核心计算 (tile MAC)
│   │   └── matmul_top.{h,cpp}  #   顶层 (AXI接口 + tile调度)
│   ├── tb/
│   │   ├── matmul_tb.cpp       #   完整测试套件 (5 用例)
│   │   └── diag_tb.cpp         #   最小诊断测试
│   ├── build/                  # 单文件综合版 (Vitis HLS clang-tidy 兼容)
│   │   ├── matmul_all.cpp      #   合并源码 (PARALLEL_N=2)
│   │   ├── run_single.tcl      #   综合 + 导出脚本 (20ns, 50MHz)
│   │   └── run.tcl             #   C仿真 + 综合脚本
│   ├── run_hls.tcl             # HLS 一键脚本
│   └── directives.tcl          # HLS 优化指令
├── vivado/
│   └── build_bd.tcl            # Vivado Block Design 自动化
├── ps_driver/
│   ├── pynq_driver.py          # Pynq (Python) 驱动
│   └── baremetal_driver.c      # 裸机 C 驱动
└── README.md
```

## 快速开始

### 环境要求

- **Vivado 2020.2** (`E:\xilinx\Vivado\2020.2`)
- **Vitis HLS 2020.2** (`E:\xilinx\Vitis_HLS\2020.2`)
- **Git Bash** (或任意 bash shell)

环境变量已在 `~/.bashrc` 和 `~/.bash_profile` 中配置，新终端自动加载。

### 运行 HLS C 仿真

```bash
cd /e/fpga-ai/hls
vitis_hls -f run_diag.tcl          # 最小诊断 (单 tile, 全1.0)
vitis_hls -f run_hls.tcl            # 完整测试 (5 用例)
```

预期输出：
```
═══ 总计: 通过 7133, 失败 0
✓ 全部通过!
```

### 运行 HLS 综合 (生成 RTL)

```bash
cd /e/fpga-ai/hls/build
vitis_hls -f run_single.tcl         # 综合 + 导出
```

生成文件位于:
- `matmul_single/solution1/syn/verilog/matmul_top.v`
- `matmul_single/solution1/syn/vhdl/matmul_top.vhd`

### Vivado Block Design 集成 (后续步骤)

```bash
cd /e/fpga-ai/vivado
vivado -mode batch -source build_bd.tcl
```

### PS 端驱动

**Pynq (Python):**
```python
from pynq_driver import MatMulAccelerator
accel = MatMulAccelerator("design_1.bit")
C = accel.matmul(A_float, B_float)
```

**裸机 C:**
将 `ps_driver/baremetal_driver.c` 导入 Vitis (原 SDK) 工程编译运行。

## 流数据协议

PS 端按以下顺序通过 DMA 发送 tile 数据：

```
for m_tile in 0..M_tiles-1:
  for n_tile in 0..N_tiles-1:
    for k_tile in 0..K_tiles-1:
      DMA.send(A_tile[m_tile][k_tile])  # TILE_M × TILE_K 个 data_t
      DMA.send(B_tile[k_tile][n_tile])  # TILE_K × TILE_N 个 data_t
    DMA.recv(C_tile[m_tile][n_tile])    # TILE_M × TILE_N 个 data_t
```

每个 tile 固定发送/接收完整的 `TILE × TILE` 元素，边界 tile 用零填充。

## 已知问题

### 1. 累加器 RAW 依赖 → II=24 (核心瓶颈)

内层 `k` 循环 `C[i][j] += A×B` 存在真正的 RAW 依赖——迭代 k+1 必须读取迭代 k 写入的 C 值。HLS 推断 II=24，即每 24 周期才能发射一次新的 k 迭代。

- **当前缓解**: -2 芯片速度等级使 Fmax 达到 143 MHz，100 MHz 时序可收敛
- **影响**: 有效 MAC 吞吐仅为理论峰值的 1/24
- **根本解决**: Systolic Array 架构——将累加器依赖链展开为空间流水线，消除 RAW 依赖，实现 II=1

### 2. 增加 MAC 数不提升吞吐

已通过实验验证: N=4 需要降频到 50 MHz 才能收敛，净吞吐与 N=2@100MHz 相同 (8.33M MAC/s)。MAC 数与吞吐的解耦关系已在上方实验表中量化。

### 3. Vivado 2020.2 IP packager bug

`export_design -format ip_catalog` 报 `bad lexical cast: core_revision` (日期戳 → int32 溢出)。

- **替代方案**: 直接使用生成的 RTL 文件 (`.v` / `.vhd`)，在 Vivado 中通过 `add_files` 手动添加

### 4. Vitis HLS 2020.2 clang-tidy 限制

- 不解析 `#include "..."` 指令 → 必须单文件综合
- 不展开 `#pragma` 参数中的 C 宏 → pragma 必须用字面量 (`factor=2` 而非 `factor=PARALLEL_N`)

## 优化路线图

- [x] **V1**: 单 MAC 单元, 功能验证通过
- [x] **V2**: 2 MAC 并行 + Tile 分块 + AXI Stream 接口 (当前基线, Fmax=143 MHz)
- [x] **V2.5**: MAC 并行度实验 — 验证了 N=4 不提升净吞吐, 确认 II=24 为根本瓶颈
- [ ] **V3**: Vivado Block Design 集成 + 上板验证
- [ ] **V4**: Systolic Array 架构 — 消除累加器 RAW 依赖, 实现 II=1, 解锁 N≥4 @100MHz
- [ ] **V5**: 4~8 MAC Systolic + Ping-Pong 双缓冲
- [ ] **V6**: AXI Master 直读 DDR, 支持大矩阵 (>64×64)

### 技术债务

| 项目 | 描述 |
|------|------|
| `run_single.tcl` 时钟 | 当前为 20ns (50MHz), 需在 N=2 正式构建时改回 10ns (100MHz) |
| `matmul_all.cpp` PARALLEL_N | 当前为 4 (实验残留), 需在正式构建时改回 2 |
