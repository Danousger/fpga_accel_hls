# FPGA AI 加速器 — 定点数矩阵乘法

基于 **Xilinx Zynq-7020** (xc7z020clg400-1) 的定点数矩阵乘法硬件加速器。

## 当前状态

| 阶段 | 状态 |
|------|------|
| C 仿真 (5 用例, 7133 元素) | ✅ 全部通过 |
| C 综合 (RTL 生成) | ✅ Verilog + VHDL |
| IP 导出 | ⚠️ Vivado 2020.2 IP packager bug |
| 时序收敛 @100MHz | ⚠️ 累加器 RAW 依赖, 内层 II=24 |
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
| 数据类型 | `ap_fixed<16, 8>` | Q8.8: 1符号 + 7整数 + 8小数 |
| 累加器 | `ap_fixed<48, 32>` | 48bit 防溢出 |
| Tile 尺寸 | 16×16×16 | 单 tile 占 ~1.5KB BRAM |
| 并行 MAC | 2 | 每周期 2 次乘累加 |
| 最大矩阵 | 64×64 | 可通过修改 `MAX_M/N/K` 增大 |
| 接口 | AXI Stream ×3 + AXI Lite ×1 | 数据流 + 控制 |

### 资源占用 (PARALLEL_N=2, 100MHz 目标)

| 资源 | 用量 | 占比 |
|------|------|------|
| DSP48E | 32 | 14% |
| BRAM | 10 | 6% |
| LUT | 11,792 | 22% |
| FF | 7,170 | 6% |

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
│   │   ├── matmul_all.cpp      #   合并源码
│   │   ├── run_single.tcl      #   综合 + 导出脚本
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

1. **时序未收敛 @100MHz**: 内层 `k` 循环的 `C[i][j] += A*B` 存在 RAW 依赖，HLS 推断 II=24
   - **缓解**: 降频到 50MHz (`create_clock -period 20.0`)
   - **根本解决**: 重构为 Systolic Array 架构
2. **IP 导出失败**: Vivado 2020.2 IP packager 的 `core_revision` 溢出 bug
   - **替代方案**: 直接使用生成的 RTL 文件 (`.v` / `.vhd`)

## 优化路线图

- [x] V1: 单 MAC 单元, 功能验证通过
- [x] V2: 2 MAC 并行 + Tile 分块 + AXI Stream 接口
- [ ] V3: Vivado Block Design 集成 + 上板验证
- [ ] V4: Systolic Array 架构 (解决时序, 实现 II=1)
- [ ] V5: 4~8 MAC 并行 + Ping-Pong 双缓冲
- [ ] V6: 支持更大矩阵 (AXI Master 直读 DDR)
