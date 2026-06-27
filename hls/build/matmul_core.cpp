// ============================================================================
// matmul_core.cpp — 矩阵乘法核心计算
// 定点数 Q8.8, TILE_M×TILE_N 输出分块, PARALLEL_N 路并行 MAC
// ============================================================================

#include "matmul_core.h"

// ---------------------------------------------------------------------------
// matmul_core — 单个 tile 的矩阵乘法
//
// 计算: C_buf[i][j] += Σ(k: 0..TILE_K-1) A_buf[i][k] × B_buf[k][j]
//
// 数据流:
//   外层 i (TILE_M) ──→ 遍历 C 的行
//   中层 j (TILE_N) ──→ 遍历 C 的列 (PARALLEL_N 路并行)
//   内层 k (TILE_K) ──→ 沿 K 维度累加 (流水线 II=1)
//
// 每个时钟周期:
//   - 读 1 个 A_buf[i][k]
//   - 读 PARALLEL_N 个 B_buf[k][j..j+PARALLEL_N-1]
//   - 做 PARALLEL_N 次乘累加
// ---------------------------------------------------------------------------

void matmul_core(
    data_t  A_buf[TILE_M][TILE_K],
    data_t  B_buf[TILE_K][TILE_N],
    acc_t   C_buf[TILE_M][TILE_N]
) {
    // -------------------------------------------------
    // HLS 数组分区 — 增加并行读带宽
    // -------------------------------------------------
    // A_buf: 沿列维度 (dim=1, K维度) cyclic 分区
    //        每次从同一行读取 1 个元素，不需要高带宽
    #pragma HLS ARRAY_PARTITION variable=A_buf cyclic factor=2 dim=2

    // B_buf: 沿列维度 (dim=1, N维度) cyclic 分区
    //        每次从同一行 k 读取 PARALLEL_N 个不同 j 的元素
    //        PARALLEL_N 路并行需要 PARALLEL_N 个独立 BRAM bank
    #pragma HLS ARRAY_PARTITION variable=B_buf cyclic factor=PARALLEL_N dim=2

    // C_buf: 沿列维度 cyclic 分区
    //        支持同时写 PARALLEL_N 个部分和
    #pragma HLS ARRAY_PARTITION variable=C_buf cyclic factor=PARALLEL_N dim=2

    // -------------------------------------------------
    // 三重循环: i (行) → j (列, 并行展开) → k (内积, 流水线)
    // -------------------------------------------------
    for (int i = 0; i < TILE_M; i++) {
        #pragma HLS LOOP_TRIPCOUNT min=1 max=TILE_M avg=TILE_M

        for (int j = 0; j < TILE_N; j += PARALLEL_N) {
            #pragma HLS LOOP_TRIPCOUNT min=1 max=TILE_N/PARALLEL_N avg=TILE_N/PARALLEL_N

            // --- 初始化部分和 ---
            // 当 j 循环开始时，从 C_buf 读出已有累加值
            // 在外层 k 累加之前完成初始化
            // （首次进入时 C_buf 应由上层函数清零）

            // --- 内层 k 循环: 沿 K 维度累加 ---
            // 此循环 PIPELINE II=1 → 每个时钟周期发射一组新的乘累加
            for (int k = 0; k < TILE_K; k++) {
                #pragma HLS LOOP_TRIPCOUNT min=1 max=TILE_K avg=TILE_K
                #pragma HLS PIPELINE II=1

                data_t a_val = A_buf[i][k];

                // PARALLEL_N 路并行乘累加
                // 展开后同时计算 4 个输出位置
                for (int p = 0; p < PARALLEL_N; p++) {
                    #pragma HLS UNROLL
                    data_t b_val = B_buf[k][j + p];
                    acc_t  product = (acc_t)a_val * (acc_t)b_val;
                    C_buf[i][j + p] += product;
                }
            }
        }
    }
}
