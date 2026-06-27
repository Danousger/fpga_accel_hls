#!/usr/bin/env python3
# ============================================================================
# pynq_driver.py — Pynq 框架下的定点数矩阵乘法加速器驱动
#
# 前置条件:
#   1. Zynq-7020 开发板 (如 Pynq-Z2, ZedBoard, ZC702 等)
#   2. 已烧录 Pynq 镜像到 SD 卡
#   3. 将 matmul_accel IP 集成到 Vivado Block Design 并生成 bitstream
#   4. 将 .bit 和 .hwh 文件上传到开发板
#
# 用法:
#   python3 pynq_driver.py
# ============================================================================

import numpy as np
import time
from pynq import Overlay, allocate
from pynq import Xlnk
import argparse


class MatMulAccelerator:
    """定点数矩阵乘法加速器 (Q8.8 格式) 的 Pynq 驱动"""

    # 量化参数: Q8.8
    Q_SCALE = 256.0       # 2^8
    Q_MIN   = -128.0
    Q_MAX   = 127.99609375  # (2^15 - 1) / 2^8

    # --- Tile 尺寸 (必须与 HLS 中的定义一致!) ---
    TILE_M = 16
    TILE_N = 16
    TILE_K = 16

    def __init__(self, bitfile_path, dma_name='axi_dma_0'):
        """
        加载 bitstream 并初始化 DMA。

        参数:
            bitfile_path: .bit 文件路径
            dma_name:     Block Design 中 AXI DMA 的实例名
        """
        print(f"[INFO] 加载 bitstream: {bitfile_path}")
        self.overlay = Overlay(bitfile_path)
        self.dma = getattr(self.overlay, dma_name)
        print(f"[INFO] DMA 已就绪: {dma_name}")

    @staticmethod
    def float_to_fixed(arr_float):
        """将 float32 numpy 数组转换为 Q8.8 int16"""
        # 钳位到 Q8.8 范围
        arr_clamped = np.clip(arr_float, MatMulAccelerator.Q_MIN,
                              MatMulAccelerator.Q_MAX)
        # 量化: int16_value = round(float * 256)
        arr_fixed = np.round(arr_clamped * MatMulAccelerator.Q_SCALE).astype(np.int16)
        return arr_fixed

    @staticmethod
    def fixed_to_float(arr_fixed):
        """将 Q8.8 int16 numpy 数组转换为 float32"""
        return arr_fixed.astype(np.float32) / MatMulAccelerator.Q_SCALE

    def matmul(self, A_float, B_float):
        """
        硬件矩阵乘法: C = A × B

        参数:
            A_float: numpy float32 数组, shape (M, K)
            B_float: numpy float32 数组, shape (K, N)

        返回:
            C_float: numpy float32 数组, shape (M, N)
        """
        M, K_in = A_float.shape
        K, N     = B_float.shape
        assert K_in == K, f"维度不匹配: A 的列数 {K_in} ≠ B 的行数 {K}"

        print(f"[INFO] 矩阵乘法: ({M}×{K}) × ({K}×{N})")

        # --- 1. 量化 ---
        A_fixed = self.float_to_fixed(A_float)
        B_fixed = self.float_to_fixed(B_float)

        # C 输出缓冲 (int32, 因为累加结果需要更宽位宽)
        C_fixed = np.zeros((M, N), dtype=np.int32)

        # --- 2. 计算 tile 数量 ---
        M_tiles = (M + self.TILE_M - 1) // self.TILE_M
        N_tiles = (N + self.TILE_N - 1) // self.TILE_N
        K_tiles = (K + self.TILE_K - 1) // self.TILE_K

        print(f"[INFO] Tile 划分: M×N×K = {M_tiles}×{N_tiles}×{K_tiles}")

        # --- 3. 分配 DMA buffer ---
        xlnk = Xlnk()

        # A tile buffer: TILE_M × TILE_K int16
        a_buf = allocate(shape=(self.TILE_M * self.TILE_K,), dtype=np.int16)
        # B tile buffer: TILE_K × TILE_N int16
        b_buf = allocate(shape=(self.TILE_K * self.TILE_N,), dtype=np.int16)
        # C tile buffer: TILE_M × TILE_N int32
        c_buf = allocate(shape=(self.TILE_M * self.TILE_N,), dtype=np.int32)

        # --- 4. 启动加速器计算 ---
        start_time = time.time()

        for m_tile in range(M_tiles):
            m_start = m_tile * self.TILE_M
            m_act   = min(self.TILE_M, M - m_start)

            for n_tile in range(N_tiles):
                n_start = n_tile * self.TILE_N
                n_act   = min(self.TILE_N, N - n_start)

                # C 的部分和 (在 K 维度累加)
                c_partial = np.zeros((self.TILE_M, self.TILE_N), dtype=np.int32)

                for k_tile in range(K_tiles):
                    k_start = k_tile * self.TILE_K
                    k_act   = min(self.TILE_K, K - k_start)

                    # --- 填充 A tile ---
                    a_buf_tile = np.zeros((self.TILE_M, self.TILE_K), dtype=np.int16)
                    a_buf_tile[:m_act, :k_act] = A_fixed[
                        m_start:m_start + m_act,
                        k_start:k_start + k_act
                    ]
                    np.copyto(a_buf, a_buf_tile.ravel())

                    # --- 填充 B tile ---
                    b_buf_tile = np.zeros((self.TILE_K, self.TILE_N), dtype=np.int16)
                    b_buf_tile[:k_act, :n_act] = B_fixed[
                        k_start:k_start + k_act,
                        n_start:n_start + n_act
                    ]
                    np.copyto(b_buf, b_buf_tile.ravel())

                    # --- DMA 传输: A → PL ---
                    self.dma.sendchannel.transfer(a_buf)
                    self.dma.sendchannel.wait()

                    # --- DMA 传输: B → PL ---
                    self.dma.sendchannel.transfer(b_buf)
                    self.dma.sendchannel.wait()

                    # --- DMA 传输: PL → C ---
                    self.dma.recvchannel.transfer(c_buf)
                    self.dma.recvchannel.wait()

                    # 累加部分和
                    c_tile = np.array(c_buf).reshape(self.TILE_M, self.TILE_N)
                    c_partial[:m_act, :n_act] += c_tile[:m_act, :n_act]

                # 写回完整结果
                C_fixed[m_start:m_start + m_act,
                        n_start:n_start + n_act] = c_partial[:m_act, :n_act]

        elapsed = time.time() - start_time

        # --- 5. 转回浮点数 ---
        C_float = self.fixed_to_float(C_fixed)

        # --- 6. 性能统计 ---
        total_ops = 2 * M * N * K  # 每个 MAC 算 2 次操作
        gops = total_ops / elapsed / 1e9
        print(f"[INFO] 耗时: {elapsed*1000:.2f} ms")
        print(f"[INFO] 吞吐: {gops:.3f} GOPS (理论)")
        print(f"[INFO] MAC 利用率: {gops / (4 * 0.1):.1%} "
              f"(4 MAC × 100MHz)")

        # --- 7. 释放 DMA buffer ---
        a_buf.close()
        b_buf.close()
        c_buf.close()
        xlnk.xlnk_reset()

        return C_float


# ============================================================================
# 测试与验证
# ============================================================================
def test_matmul(accel, M, N, K):
    """测试矩阵乘法并验证正确性"""
    print(f"\n{'='*60}")
    print(f"测试: ({M}×{K}) × ({K}×{N})")
    print(f"{'='*60}")

    # 生成随机测试数据 (限制范围防止溢出)
    np.random.seed(42)
    A = (np.random.rand(M, K).astype(np.float32) - 0.5) * 20.0
    B = (np.random.rand(K, N).astype(np.float32) - 0.5) * 20.0

    # 硬件计算
    C_hw = accel.matmul(A, B)

    # 软件参考 (numpy)
    C_sw = np.dot(A, B)

    # 误差分析
    abs_err = np.abs(C_hw - C_sw)
    max_err = np.max(abs_err)
    mean_err = np.mean(abs_err)
    rel_err = abs_err / (np.abs(C_sw) + 1e-8)
    max_rel_err = np.max(rel_err)

    print(f"[验证] 最大绝对误差: {max_err:.6f}")
    print(f"[验证] 平均绝对误差: {mean_err:.6f}")
    print(f"[验证] 最大相对误差: {max_rel_err*100:.4f}%")

    # Q8.8 理论精度: 1 LSB = 1/256 ≈ 0.004
    if max_err < 0.02:
        print(f"[验证] ✓ 通过 (误差在 Q8.8 精度范围内)")
    else:
        print(f"[验证] ✗ 误差超出预期, 请检查!")

    return max_err


def main():
    parser = argparse.ArgumentParser(
        description='定点数矩阵乘法加速器 (Q8.8) Pynq 驱动')
    parser.add_argument('--bit', default='design_1.bit',
                        help='Bitstream 文件路径')
    parser.add_argument('--dma', default='axi_dma_0',
                        help='AXI DMA 实例名称')
    parser.add_argument('--test-only', action='store_true',
                        help='只做自检 (需要开发板连接)')
    args = parser.parse_args()

    print("╔══════════════════════════════════════════╗")
    print("║  FPGA 矩阵乘法加速器 — Pynq 驱动 v1.0   ║")
    print("║  定点格式: Q8.8, Tile: 16×16×16         ║")
    print("╚══════════════════════════════════════════╝")

    try:
        accel = MatMulAccelerator(args.bit, args.dma)

        # 测试 1: 小矩阵
        test_matmul(accel, 16, 16, 16)

        # 测试 2: 中等矩阵
        test_matmul(accel, 48, 48, 48)

        # 测试 3: 非对齐
        test_matmul(accel, 13, 17, 11)

        # 测试 4: 矩形矩阵
        test_matmul(accel, 8, 32, 16)

        print(f"\n{'='*60}")
        print("全部测试完成!")
        print(f"{'='*60}")

    except Exception as e:
        print(f"[错误] {e}")
        print("\n请确认:")
        print("  1. 开发板已连接并运行 Pynq")
        print(f"  2. Bitstream 文件 '{args.bit}' 存在")
        print("  3. Block Design 中 DMA 实例名正确")


if __name__ == '__main__':
    main()
