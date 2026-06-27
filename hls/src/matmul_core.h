// ============================================================================
// matmul_core.h — 矩阵乘法核心计算模块 头文件
// ============================================================================

#ifndef MATMUL_CORE_H
#define MATMUL_CORE_H

#include "matmul.h"

// ============================================================================
// 平铺矩阵乘法核心
//
// C[M][N] = A[M][K] × B[K][N]
//
// 计算策略 (输出驻留 Outer-Product 的变体):
//   1. 将 C 按 TILE_M × TILE_N 分块
//   2. 每个 tile 内部，沿 K 维度累加 partial sum
//   3. 最内层循环做 PARALLEL_N 路并行乘累加
//
// 内存带宽优化:
//   - A 和 B 的 tile buffer 做 ARRAY_PARTITION (cyclic)
//   - 每个时钟周期可同时读取 PARALLEL_N 个 A 和 B 元素
//
// 时序优化:
//   - 内层 k 循环 PIPELINE II=1
//   - 并行 MAC 通过 UNROLL 实现
// ============================================================================

void matmul_core(
    data_t  A_buf[TILE_M][TILE_K],   // A 的 tile (行主序)
    data_t  B_buf[TILE_K][TILE_N],   // B 的 tile (正常行列，未转置)
    acc_t   C_buf[TILE_M][TILE_N]    // C 的 tile (累加结果)
);

#endif // MATMUL_CORE_H
