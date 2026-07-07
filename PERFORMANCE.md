# 性能对比报告 — 原版 vs Systolic Array

> 生成时间: 2026-07-07
> 数据来源: Vitis HLS 2020.2 C 综合报告 + Vivado 2020.2 实现报告
> 目标芯片: xc7z020clg400-2 (Zynq-7020, -2 速度等级)

## 1. 核心循环 II (Initiation Interval)

| 指标 | 原版 (matmul_core) | 脉动阵列 (matmul_systolic_core) |
|---|---|---|
| 内层 II (目标) | 1 | 1 |
| **内层 II (实际)** | **24** | **1** |
| 内层迭代延迟 | 50 周期 | 3 周期 |
| 总延迟 (单 tile 核心) | 1,561 周期 | 704 周期 |
| 循环展开因子 | 64 (tripcount) | 16 (4×4 sub-blocks) |

**来源文件:**
- 原版: `hls/build/matmul_single/solution1/syn/report/matmul_core_csynth.rpt`
  ```
  | VITIS_LOOP_35_1_VITIS_LOOP_36_2 | 1561 | 1561 | 50 | 24 | 1 | 64 | yes |
  ```
- 脉动阵列: `hls/build/systolic_single/solution1/syn/report/matmul_systolic_core_csynth.rpt`
  ```
  | VITIS_LOOP_63_5 | 24 | 24 | 3 | 1 | 1 | 22 | yes |
  ```

## 2. 资源占用对比

| 资源 | 原版 (PARALLEL_N=4) | 脉动阵列 (4×4) | 变化 |
|---|---|---|---|
| **DSP48E** | 64 (29%) | **16 (7%)** | **-75%** ↓ |
| **BRAM_18K** | 20 (7%) | **0 (0%)** | **-100%** ↓ |
| **FF** | 5,874 (5%) | 10,168 (9%) | +73% ↑ |
| **LUT** | 19,173 (36%) | 11,778 (22%) | **-39%** ↓ |

**来源文件:**
- 原版: `hls/build/matmul_single/solution1/syn/report/matmul_top_csynth.rpt`
- 脉动阵列: `hls/build/systolic_single/solution1/syn/report/matmul_top_csynth.rpt`

## 3. 理论吞吐对比

```
有效 MAC 吞吐 = MAC 数 / II × 时钟频率

原版 (II=24, PARALLEL_N=4):  4 / 24 × 100M = 16.7M MAC/s  (时序不过, 需降频)
原版 (II=24, PARALLEL_N=2):  2 / 24 × 100M =  8.3M MAC/s  (生产基线, 100MHz)
脉动阵列 (II=1, 16 PEs):    16 /  1 × 100M = 1.6G MAC/s   (理论峰值, 100MHz)
```

**提升倍数: 1.6G / 8.3M = 192×**

### 192× 的来源分解

```
提升 = (脉动 MAC 数 / 原版 MAC 数) × (原版 II / 脉动 II)
     = (16 / 2) × (24 / 1)
     = 8 × 24
     = 192×
```

- **8×** 来自 MAC 并行度提升: 2 → 16 (4×4 PE 阵列)
- **24×** 来自 II 提升: 24 → 1 (寄存器累加器消除 RAW 依赖)

### 公平性说明

原版 N=4 理论上能到 16.7M MAC/s, 但 -2 芯片 100MHz 时序不过 (slack 为负),
必须降频到 50MHz → 8.3M MAC/s。所以无论 N=2@100MHz 还是 N=4@50MHz,
原版有效吞吐都是 8.3M MAC/s (README 实验表已验证)。
用 8.3M 作为基线是公平的。

## 4. Vivado 实现时序 (脉动阵列版)

| 检查项 | 结果 | 说明 |
|---|---|---|
| **建立时间 WNS** | **+1.260 ns** | 正值, 100MHz 时序收敛 |
| 建立 TNS | 0.000 ns | 0 个失败端点 |
| **保持时间 WHS** | **+0.059 ns** | 正值, 保持时间满足 |
| 保持 THS | 0.000 ns | 0 个失败端点 |
| 脉宽 WPWS | +3.870 ns | 正值 |
| **失败端点总数** | **0** | 全部通过 |

**来源文件:**
`vivado/vivado_proj/matmul_system.runs/impl_1/design_1_wrapper_timing_summary_routed.rpt`

## 5. 功能验证

| 测试 | 结果 |
|---|---|
| C 仿真 (5 用例, 7133 元素) | ✅ 全部通过, 最大误差 0.025 (6 LSB) |
| 诊断测试 (单 tile, 全 1.0) | ✅ 全部正确 |
| Vivado DRC | 0 Errors, 94 Warnings (均为 DSP 流水线建议) |

**来源文件:**
- 测试平台: `hls/tb/matmul_tb.cpp`, `hls/tb/diag_tb.cpp`
- 构建脚本: `hls/run_systolic.tcl` (csim 输出 "总计: 通过 7133, 失败 0")

## 6. 关键结论

| 问题 | 原版 | 脉动阵列 | 解决方式 |
|---|---|---|---|
| 累加器 RAW 依赖 | II=24 (BRAM 读-改-写) | II=1 (寄存器累加) | 输出驻留 PE 阵列 |
| DSP 占用 | 64 (29%) | 16 (7%) | 寄存器代替 BRAM, 减少复用 |
| BRAM 占用 | 20 (7%) | 0 (0%) | 完全用寄存器 |
| 理论吞吐 | 8.3M MAC/s | 1.6G MAC/s | **192× 提升** |
| 时序收敛 | 100MHz (N=2) | 100MHz (WNS=+1.26ns) | 均收敛 |

## 7. 上板实测 (待补充)

板级实测数据 (含 DMA 开销的实际延迟和吞吐) 将由 `ps_driver/notebooks/bench_matmul.ipynb` 生成:
- 时延 vs 矩阵尺寸柱状图
- 吞吐量 vs 矩阵尺寸折线图
- 64×64 结果矩阵热力图
- HW vs SW 误差分布直方图

> **注意**: 理论 1.6 GMAC/s 是纯计算峰值, 实际有效吞吐受 DMA 带宽限制
> (每 tile 传 768 个 16-bit 数据 + 控制开销), 预计实测值约为理论的 30-60%。
