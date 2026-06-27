// ============================================================================
// matmul_top.cpp — 矩阵乘法顶层模块
//
// 架构:
//   ┌─────────────────────────────────────────────────┐
//   │  matmul_top                                     │
//   │  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │
//   │  │ in_A     │  │ in_B     │  │  matmul_core │  │
//   │  │ stream   │  │ stream   │  │  (tile MAC)  │  │
//   │  │    ↓     │  │    ↓     │  │      ↓       │  │
//   │  │ A_buf    │  │ B_buf    │  │   C_buf      │  │
//   │  │[TM][TK]  │  │[TK][TN]  │  │  [TM][TN]    │──▶ out_C
//   │  └──────────┘  └──────────┘  └──────────────┘  │
//   │                                                  │
//   │  控制逻辑: M_tiles × N_tiles × K_tiles 三重循环  │
//   └─────────────────────────────────────────────────┘
// ============================================================================

#include "matmul_top.h"
#include "matmul_core.h"

// 辅助宏: 取两数最小值
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

void matmul_top(
    data_stream &in_A,
    data_stream &in_B,
    data_stream &out_C,
    int          M,
    int          N,
    int          K
) {
    // -------------------------------------------------
    // 接口 pragma: 综合为 AXI 总线
    // -------------------------------------------------
    #pragma HLS INTERFACE axis      port=in_A
    #pragma HLS INTERFACE axis      port=in_B
    #pragma HLS INTERFACE axis      port=out_C
    #pragma HLS INTERFACE s_axilite port=M
    #pragma HLS INTERFACE s_axilite port=N
    #pragma HLS INTERFACE s_axilite port=K
    #pragma HLS INTERFACE s_axilite port=return

    // -------------------------------------------------
    // 片内 tile buffer (BRAM)
    // -------------------------------------------------
    data_t A_buf[TILE_M][TILE_K];
    data_t B_buf[TILE_K][TILE_N];
    acc_t  C_buf[TILE_M][TILE_N];

    // 数组分区 — 为 matmul_core 提供并行读带宽
    #pragma HLS ARRAY_PARTITION variable=A_buf cyclic factor=2 dim=2
    #pragma HLS ARRAY_PARTITION variable=B_buf cyclic factor=PARALLEL_N dim=2
    #pragma HLS ARRAY_PARTITION variable=C_buf cyclic factor=PARALLEL_N dim=2

    // -------------------------------------------------
    // 计算各维度的 tile 数量
    // -------------------------------------------------
    int M_tiles = (M + TILE_M - 1) / TILE_M;
    int N_tiles = (N + TILE_N - 1) / TILE_N;
    int K_tiles = (K + TILE_K - 1) / TILE_K;

    // -------------------------------------------------
    // 三重 tile 循环
    //
    // 外层: M 维度 (C 的行方向)
    // 中层: N 维度 (C 的列方向)
    // 内层: K 维度 (A 的列 / B 的行, 部分累加)
    //
    // 数据调度策略 (输出驻留):
    //   - C_buf 在 K 循环内不断累加
    //   - A_buf 在 K 循环每次迭代重新加载
    //   - B_buf 在 K 循环每次迭代重新加载
    //   - K 循环结束后, C_buf 写出到输出流
    // -------------------------------------------------

    for (int m_tile = 0; m_tile < M_tiles; m_tile++) {
        #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_M/TILE_M

        // 当前 M tile 的实际尺寸 (边界 tile 可能不满)
        int m_actual = MIN(TILE_M, M - m_tile * TILE_M);

        for (int n_tile = 0; n_tile < N_tiles; n_tile++) {
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_N/TILE_N

            int n_actual = MIN(TILE_N, N - n_tile * TILE_N);

            // --- 清零 C_buf (新一轮 K 累加开始) ---
            for (int i = 0; i < TILE_M; i++) {
                #pragma HLS LOOP_TRIPCOUNT min=TILE_M max=TILE_M
                for (int j = 0; j < TILE_N; j++) {
                    #pragma HLS LOOP_TRIPCOUNT min=TILE_N max=TILE_N
                    #pragma HLS PIPELINE II=1
                    C_buf[i][j] = 0;
                }
            }

            // --- K 维度累加循环 ---
            for (int k_tile = 0; k_tile < K_tiles; k_tile++) {
                #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_K/TILE_K

                int k_actual = MIN(TILE_K, K - k_tile * TILE_K);

                // === 步骤 1: 从流中加载 A_tile ===
                // 始终读取 TILE_M × TILE_K 个元素 (padding 位置由发送端填零)
                for (int i = 0; i < TILE_M; i++) {
                    for (int k = 0; k < TILE_K; k++) {
                        #pragma HLS PIPELINE II=1
                        in_A >> A_buf[i][k];
                    }
                }

                // === 步骤 2: 从流中加载 B_tile ===
                // 始终读取 TILE_K × TILE_N 个元素
                for (int k = 0; k < TILE_K; k++) {
                    for (int j = 0; j < TILE_N; j++) {
                        #pragma HLS PIPELINE II=1
                        in_B >> B_buf[k][j];
                    }
                }

                // === 步骤 3: 调用核心计算单元 ===
                // C_buf += A_buf × B_buf (部分积累加)
                matmul_core(A_buf, B_buf, C_buf);

            } // k_tile 循环结束

            // --- K 累加完成, 写出 C_tile 到输出流 ---
            // 始终写出 TILE_M × TILE_N 个元素 (padding 位置写零)
            for (int i = 0; i < TILE_M; i++) {
                for (int j = 0; j < TILE_N; j++) {
                    #pragma HLS PIPELINE II=1
                    // 饱和截断: acc_t → data_t, padding 值始终为零
                    out_C << (data_t)C_buf[i][j];
                }
            }

        } // n_tile
    } // m_tile
}
