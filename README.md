# Zynq-7020 脉动阵列矩阵乘法器

本项目在 Zynq-7020 上实现了一个基于 Vitis HLS 的定点矩阵乘法加速器。PL 端采用 4×4 输出驻留（output-stationary）脉动阵列，PS 端通过 AXI DMA 传输矩阵，并使用 AXI4-Lite 配置矩阵尺寸和启动计算。

当前分支只保留脉动阵列架构和对应的板级集成流程，旧的并行 MAC 实验及未验证驱动已经移除。

## 设计参数

| 项目 | 当前配置 |
|---|---|
| FPGA | Zynq-7020，`xc7z020clg400-2` |
| PL 时钟 | 100 MHz |
| 脉动阵列 | 4×4，共 16 个 PE / DSP |
| 输入与输出 | 有符号 Q8.8，`ap_fixed<16,8,AP_RND,AP_SAT>` |
| 累加器 | 48 位，`ap_fixed<48,32,...>` |
| tile | 16×16×16 |
| AXI Stream | 32 位；Q8.8 原始值位于 `TDATA[15:0]` |
| 控制接口 | AXI4-Lite：`M=0x10`、`N=0x18`、`K=0x20` |
| 数据通路 | A、B 两路 MM2S，C 一路 S2MM |

每个 A、B tile 均包含 256 个 AXIS word。每个 C tile 同样包含 256 个 word，加速器在第 256 个输出上置位 `TLAST`，使 AXI DMA S2MM 正确结束本次传输。

## 理论性能

矩阵乘法的运算量按一次乘法和一次加法计为 2 OPS：

```text
OPS = 2 × M × N × K
```

对 16×16×16 的单 tile，共有 8192 OPS。

### 1. 阵列瞬时峰值

4×4 阵列在稳定状态下每周期可执行 16 次 MAC：

```text
峰值 = 16 MAC/cycle × 2 OPS/MAC × 100 MHz = 3.2 GOPS
```

这是 PE 全部有效工作的瞬时算术峰值，不包含阵列填充、排空、tile 切换、AXIS 传输或软件控制开销。

### 2. HLS 核心的单 tile 理论吞吐

Vitis HLS 2020.2 综合报告显示，`matmul_systolic_core` 处理一个 16×16×16 tile 的延迟为 705 周期。由此得到：

```text
核心延迟 = 705 / 100 MHz = 7.05 us
核心吞吐 = 8192 OPS / 7.05 us ≈ 1.162 GOPS
阵列峰值利用率 ≈ 1.162 / 3.2 = 36.3%
```

利用率低于瞬时峰值的主要原因是：16×16 输出被拆成 16 个 4×4 子块依次计算，每个子块都存在初始化、填充、排空和写回开销。

### 3. PL 顶层单 tile 上界

当前顶层没有使用 `DATAFLOW`，初始化、输入装载、核心计算和输出写回是串行阶段。根据 HLS 循环延迟，可近似分解为：

| 阶段 | 周期 |
|---|---:|
| 清零 C buffer | 256 |
| 装载 A、B 并调用核心 | 约 1223 |
| 输出 C | 258 |
| 合计 | 约 1737 |

因此，忽略 DMA/DDR 阻塞和 AXI4-Lite 软件控制时：

```text
PL 数据路径延迟 ≈ 17.37 us
PL 数据路径吞吐上界 ≈ 8192 / 17.37 us ≈ 0.472 GOPS
相对 3.2 GOPS 峰值约为 14.8%
```

该数值是由综合报告估算的硬件上界，不是板级实测结果。Jupyter 中测得的是 PS 观察到的端到端延迟，还包括 Python 调用、寄存器访问、DMA 配置、DDR 访问、缓存维护和轮询开销，因此通常会更低。若要准确测量纯硬件周期，应在 PL 中加入 AXI Timer 或性能计数器。

### 4. AXIS 带宽上界

每路 AXIS 为 32 位、工作在 100 MHz，连续传输时单向理论带宽为：

```text
32 bit × 100 MHz = 400 MB/s ≈ 381.5 MiB/s
```

一次单 tile 运算传输 A、B、C 共 768 个 32 位 word，即 3072 字节。这个带宽值同样不包含突发间隙、DMA 描述符配置、DDR 仲裁和软件开销。

## 目录结构

```text
hls/src/
  matmul_systolic_core.cpp     4×4 脉动阵列核心
  matmul_systolic_top.cpp      tiling、AXIS 与 AXI4-Lite 顶层
  matmul*.h                    数据类型和接口声明
hls/tb/matmul_tb.cpp           算术、边界尺寸与 TLAST C 仿真
hls/build/
  matmul_systolic_all.cpp      Vitis HLS 2020.2 使用的扁平化源码
  run_systolic_single.tcl      单文件综合脚本
vivado/build_bd_systolic.tcl   PS7、三路 DMA 与 HLS RTL 集成
ps_driver/notebooks/
  axis_test.ipynb              板级功能验证
  benchmark_matmul.ipynb       性能测试与可视化
deploy.ps1                     bit/hwh 部署脚本
```

`hls/src/` 是可读源码的来源。由于 Vitis HLS 2020.2 的兼容性限制，修改脉动阵列代码时必须同步维护 `hls/build/matmul_systolic_all.cpp`，否则两条综合路径会产生不同硬件。

## 构建与仿真

使用 Git Bash，并确保已经加载 Vitis/Vivado 2020.2 环境。

```bash
cd /e/fpga-ai/hls
vitis_hls -f run_systolic.tcl

cd /e/fpga-ai/hls/build
vitis_hls -f run_systolic_single.tcl

cd /e/fpga-ai/vivado
vivado -mode batch -source build_bd_systolic.tcl
```

目标器件必须保持为 `xc7z020clg400-2`。生成 bitstream 前，应确认 RTL 中存在 `out_C_TLAST`，接口前缀可能因 HLS 版本带有 `_V`。

## 部署到 Pynq

Vivado 构建完成后，在项目根目录运行：

```powershell
.\deploy.ps1
```

当前板卡地址为 `192.168.2.99`，Jupyter 服务端口为 `9090`。建议按以下顺序运行：

1. `axis_test.ipynb`：先验证 DMA、TLAST 和计算结果。
2. `benchmark_matmul.ipynb`：10 次预热后执行 100 次采样，显示延迟、GOPS、有效 DMA 带宽和误差热力图。

性能 notebook 使用已经过板级验证的对象映射：`axi_dma_0` 负责 A MM2S 与 C S2MM，`axi_dma_1` 负责 B MM2S。其结果口径为 PS 端到端性能，不应与 3.2 GOPS 的阵列瞬时峰值直接等同。

## DDR 配置注意事项

PS7 DDR 器件必须选择 `MT41K256M16 RE-125`。此前错误的 memory part 只暴露 512 MiB 地址空间，而实际板卡和 Pynq 镜像使用 1 GiB。DMA buffer 若被分配到 512 MiB 以上区域，DMA 就无法访问，表现上很像 AXIS 或 `wait()` 挂死。

此外，FCLK0 必须通过 Vivado 的时钟关联正确写入 HWH，并由 Pynq 时钟框架启用。PL 无时钟时，首次 AXI-Lite 访问可能直接导致系统总线挂死。

## 当前限制

- 板级性能测试目前聚焦于已经验证的单个 16×16×16 tile。
- 顶层会为多 tile 输出分别产生 `TLAST`；在扩展到更大矩阵前，需要同时设计与验证 DMA 分包和 PS 侧 tile 调度策略。
- 当前架构的装载、计算、写回未重叠；进一步优化可考虑 ping-pong buffer、`DATAFLOW` 和批量硬件计时。
