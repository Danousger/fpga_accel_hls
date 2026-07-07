#!/usr/bin/env python3
# ============================================================================
# pynq_driver.py — Systolic Array 矩阵乘法加速器 Pynq 驱动
#
# 适配:
#   - Pynq v2.6+ (allocate API; v2.5 用 Xlnk 回退)
#   - Block Design: 3 个单向 AXI DMA (axi_dma_A/B/C) + matmul_top_0 IP
#   - HLS 流协议: 每 (m,n) tile 发送全部 A/B k_tile 后接收一次 C_tile
#
# 前置条件:
#   1. Pynq v2.7 镜像 (Ubuntu 20.04) 已烧录 SD 卡
#   2. design_1_wrapper.bit + design_1.hwh 已传到板子
#
# 用法:
#   from pynq_driver import MatMulAccelerator
#   accel = MatMulAccelerator("design_1.bit")
#   C = accel.matmul(A, B)            # 单次计算 + 计时
#   results = accel.benchmark()       # 多尺寸基准测试
# ============================================================================

import numpy as np
import time
from pynq import Overlay, allocate, MMIO, Clocks

# Pynq v2.5 兼容 (Xlnk); v2.6+ 用 allocate
try:
    from pynq import Xlnk
    _HAS_XLNK = True
except ImportError:
    _HAS_XLNK = False


def _enable_fclk():
    """使能 FCLK_CLK0 = 100MHz + AXI 总线时钟。

    Linux clk-fclk-zynq 驱动 claim 了 FCLK 寄存器, 直接写 SLCR 会被忽略。
    必须用 Pynq Clocks API (走 kernel clock framework) 来使能 FCLK。

    顺序: 先使能 FCLK (PL 时钟), 再使能 AXI 总线时钟, 否则总线无时钟会崩。
    """
    # 1) 通过 Pynq Clocks API 设置 FCLK0 = 100MHz (kernel clock framework)
    if Clocks.fclk0 != 100:
        Clocks.fclk0 = 100
        time.sleep(0.2)

    # 2) 使能 AXI 总线时钟 (APER_CLK_CTRL, SLCR+0x138, 无写保护)
    slcr = MMIO(0xF8000000, 0x1000)
    aper = slcr.read(0x138)
    slcr.write(0x138, aper | (1 << 0) | (1 << 4))   # M_AXI_GP0 + S_AXI_HP0
    time.sleep(0.1)


class MatMulAccelerator:
    """Systolic Array 定点矩阵乘法加速器 (Q8.8) 的 Pynq 驱动"""

    Q_SCALE = 256.0
    Q_MIN   = -128.0
    Q_MAX   = 127.99609375

    # Tile 尺寸 (必须与 HLS matmul.h 一致)
    TILE_M = 16
    TILE_N = 16
    TILE_K = 16

    # HLS s_axi_control 寄存器偏移 (从 RTL matmul_top_control_s_axi.v 确认)
    REG_AP_CTRL = 0x00   # bit0=start, bit1=done, bit2=idle, bit3=ready
    REG_M        = 0x10
    REG_N        = 0x18
    REG_K        = 0x20

    def __init__(self, bitfile_path="design_1.bit"):
        """
        加载 bitstream 并初始化 3 个 DMA + 加速器 IP。

        参数:
            bitfile_path: .bit 文件路径 (.hwh 需同名同目录)
        """
        print(f"[INFO] 加载 bitstream: {bitfile_path}")
        self.overlay = Overlay(bitfile_path)

        # 3 个单向 DMA (与 build_bd_systolic.tcl 中的实例名一致)
        self.dma_A = self.overlay.axi_dma_A   # MM2S → in_A
        self.dma_B = self.overlay.axi_dma_B   # MM2S → in_B
        self.dma_C = self.overlay.axi_dma_C   # S2MM ← out_C

        # HLS IP (Pynq 从 .hwh 自动生成驱动)
        self.ip = self.overlay.matmul_top_0

        # 手动使能 FCLK_CLK0 + AXI 总线时钟 (Pynq v2.7 可能不应用 PS7 init)
        _enable_fclk()

        print("[INFO] Overlay 加载完成: 3 DMA + matmul_top_0 + FCLK 已使能")

    # ------------------------------------------------------------------
    # 量化辅助
    # ------------------------------------------------------------------
    @staticmethod
    def float_to_fixed(arr_float):
        """float32 → Q8.8 int16"""
        arr = np.clip(arr_float, MatMulAccelerator.Q_MIN, MatMulAccelerator.Q_MAX)
        return np.round(arr * MatMulAccelerator.Q_SCALE).astype(np.int16)

    @staticmethod
    def fixed_to_float(arr_fixed):
        """Q8.8 int16 → float32"""
        return arr_fixed.astype(np.float32) / MatMulAccelerator.Q_SCALE

    # ------------------------------------------------------------------
    # 硬件矩阵乘法
    # ------------------------------------------------------------------
    def matmul(self, A_float, B_float):
        """
        硬件矩阵乘法: C = A × B

        参数:
            A_float: float32 (M, K)
            B_float: float32 (K, N)
        返回:
            C_float: float32 (M, N)
            latency_ms: 硬件计算耗时 (毫秒, 含 DMA)
        """
        M, K_in = A_float.shape
        K, N = B_float.shape
        assert K_in == K, f"维度不匹配: A 列 {K_in} ≠ B 行 {K}"

        A_fixed = self.float_to_fixed(A_float)
        B_fixed = self.float_to_fixed(B_float)
        C_fixed = np.zeros((M, N), dtype=np.int16)

        M_tiles = (M + self.TILE_M - 1) // self.TILE_M
        N_tiles = (N + self.TILE_N - 1) // self.TILE_N
        K_tiles = (K + self.TILE_K - 1) // self.TILE_K

        # DMA buffer (int16, 物理连续; 每个 tile 固定 TILE×TILE 元素)
        a_buf = allocate(self.TILE_M * self.TILE_K, dtype=np.int16)
        b_buf = allocate(self.TILE_K * self.TILE_N, dtype=np.int16)
        c_buf = allocate(self.TILE_M * self.TILE_N, dtype=np.int16)

        # 配置加速器 + 启动
        self.ip.write(self.REG_M, M)
        self.ip.write(self.REG_N, N)
        self.ip.write(self.REG_K, K)
        self.ip.write(self.REG_AP_CTRL, 0x1)   # ap_start

        t0 = time.perf_counter()

        for mt in range(M_tiles):
            ms = mt * self.TILE_M
            ma = min(self.TILE_M, M - ms)
            for nt in range(N_tiles):
                ns = nt * self.TILE_N
                na = min(self.TILE_N, N - ns)

                # 发送全部 k_tile 的 A + B (IP 内部累加 K 维度)
                for kt in range(K_tiles):
                    ks = kt * self.TILE_K
                    ka = min(self.TILE_K, K - ks)

                    # 填充 A tile (零填充边界)
                    a_tile = np.zeros((self.TILE_M, self.TILE_K), dtype=np.int16)
                    a_tile[:ma, :ka] = A_fixed[ms:ms+ma, ks:ks+ka]
                    np.copyto(a_buf, a_tile.reshape(-1))

                    # 填充 B tile
                    b_tile = np.zeros((self.TILE_K, self.TILE_N), dtype=np.int16)
                    b_tile[:ka, :na] = B_fixed[ks:ks+ka, ns:ns+na]
                    np.copyto(b_buf, b_tile.reshape(-1))

                    # 并行发送 A + B (两个 MM2S 独立 stream)
                    self.dma_A.sendchannel.transfer(a_buf)
                    self.dma_B.sendchannel.transfer(b_buf)
                    self.dma_A.sendchannel.wait()
                    self.dma_B.sendchannel.wait()

                # 接收 C tile (IP 完成该 (m,n) tile 的全部 K 累加后输出)
                self.dma_C.recvchannel.transfer(c_buf)
                self.dma_C.recvchannel.wait()

                c_tile = np.array(c_buf).reshape(self.TILE_M, self.TILE_N)
                C_fixed[ms:ms+ma, ns:ns+na] = c_tile[:ma, :na]

        t1 = time.perf_counter()
        latency_ms = (t1 - t0) * 1000.0

        # 等待 ap_done (确保 IP 真正完成)
        while (self.ip.read(self.REG_AP_CTRL) & 0x2) == 0:
            pass

        a_buf.close()
        b_buf.close()
        c_buf.close()

        C_float = self.fixed_to_float(C_fixed)
        return C_float, latency_ms

    # ------------------------------------------------------------------
    # 软件参考 (numpy)
    # ------------------------------------------------------------------
    @staticmethod
    def matmul_sw(A_float, B_float):
        return np.dot(A_float, B_float)

    # ------------------------------------------------------------------
    # 基准测试: 多尺寸, 多次运行, 返回结构化结果
    # ------------------------------------------------------------------
    def benchmark(self, sizes=(16, 32, 48, 64), runs=5, seed=42):
        """
        多尺寸基准测试。

        参数:
            sizes: 矩阵尺寸列表 (M=N=K=size)
            runs:  每个尺寸重复次数 (取中位数)
            seed:  随机种子 (可复现)
        返回:
            dict: {size: {"latency_ms", "latency_std", "gops",
                          "max_err", "mean_err", "pass"}}
        """
        results = {}
        np.random.seed(seed)

        for size in sizes:
            M = N = K = size
            # 数据范围确保 K 次累加不饱和 (K × scale² < 128)
            scale = min(8.0, 8.0 / (size ** 0.5))
            A = (np.random.rand(M, K).astype(np.float32) - 0.5) * 2 * scale
            B = (np.random.rand(K, N).astype(np.float32) - 0.5) * 2 * scale

            C_sw = self.matmul_sw(A, B)
            latencies = []
            max_errs = []
            for r in range(runs):
                C_hw, lat = self.matmul(A, B)
                latencies.append(lat)
                err = np.abs(C_hw - C_sw)
                max_errs.append(float(err.max()))

            latencies = np.array(latencies)
            med = float(np.median(latencies))
            std = float(np.std(latencies))
            total_ops = 2 * M * N * K
            gops = total_ops / (med / 1000.0) / 1e9
            max_err = float(np.max(max_errs))
            mean_err = float(np.mean([
                np.abs(self.matmul(A, B)[0] - C_sw).mean()
                for _ in range(2)
            ]))

            results[size] = {
                "latency_ms": med,
                "latency_std": std,
                "gops": gops,
                "max_err": max_err,
                "mean_err": mean_err,
                "pass": max_err < 0.03,
                "raw_latencies": latencies.tolist(),
            }
            status = "✓" if max_err < 0.03 else "✗"
            print(f"  [{status}] {size}×{size}×{size}: "
                  f"{med:.2f}±{std:.2f} ms, {gops:.2f} GOPS, "
                  f"max_err={max_err:.4f}")

        return results


# ============================================================================
# 命令行自检
# ============================================================================
def main():
    import argparse
    parser = argparse.ArgumentParser(description="Systolic matmul Pynq 驱动")
    parser.add_argument("--bit", default="design_1.bit", help="Bitstream 路径")
    parser.add_argument("--bench", action="store_true", help="运行基准测试")
    args = parser.parse_args()

    accel = MatMulAccelerator(args.bit)

    if args.bench:
        print("\n=== 基准测试 ===")
        results = accel.benchmark()
        print("\n=== 汇总 ===")
        for size, r in results.items():
            print(f"  {size}×{size}: {r['latency_ms']:.2f} ms, "
                  f"{r['gops']:.2f} GOPS, {'PASS' if r['pass'] else 'FAIL'}")
    else:
        A = (np.random.rand(16, 16).astype(np.float32) - 0.5) * 4
        B = (np.random.rand(16, 16).astype(np.float32) - 0.5) * 4
        C_hw, lat = accel.matmul(A, B)
        C_sw = np.dot(A, B)
        err = np.abs(C_hw - C_sw).max()
        print(f"16×16: {lat:.2f} ms, max_err={err:.4f}, "
              f"{'PASS' if err < 0.03 else 'FAIL'}")


if __name__ == "__main__":
    main()
