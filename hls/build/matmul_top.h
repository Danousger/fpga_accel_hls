// ============================================================================
// matmul_top.h — 矩阵乘法顶层模块 头文件
// 接口: AXI Stream 输入 (A, B), AXI Stream 输出 (C), AXI Lite 控制
// ============================================================================

#ifndef MATMUL_TOP_H
#define MATMUL_TOP_H

#include "matmul.h"

// ============================================================================
// matmul_top — 定点数矩阵乘法顶层函数
//
// 计算: C[M×N] = A[M×K] × B[K×N]
//
// 流数据协议 (由 PS 端 DMA 驱动):
//   for m_tile in 0..ceil(M/TILE_M):
//     for n_tile in 0..ceil(N/TILE_N):
//       for k_tile in 0..ceil(K/TILE_K):
//         发送 A_tile[m_tile][k_tile] → in_A (TILE_M × TILE_K 个 data_t)
//         发送 B_tile[k_tile][n_tile] → in_B (TILE_K × TILE_N 个 data_t)
//       接收 C_tile[m_tile][n_tile] ← out_C (TILE_M × TILE_N 个 data_t)
//
// 参数:
//   in_A  : AXI Stream 输入 — 矩阵 A 的分块数据
//   in_B  : AXI Stream 输入 — 矩阵 B 的分块数据
//   out_C : AXI Stream 输出 — 矩阵 C 的分块结果
//   M     : 矩阵 A 的行数 (≤ MAX_M)
//   N     : 矩阵 B 的列数 (≤ MAX_N)
//   K     : 矩阵 A 的列数 = 矩阵 B 的行数 (≤ MAX_K)
// ============================================================================

void matmul_top(
    data_stream &in_A,
    data_stream &in_B,
    data_stream &out_C,
    int          M,
    int          N,
    int          K
);

#endif // MATMUL_TOP_H
