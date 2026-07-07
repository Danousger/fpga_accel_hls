// ============================================================================
// matmul_systolic_top.cpp — 脉动阵列版顶层模块
//
// 接口与 matmul_top.cpp 完全一致 (AXI Stream ×3 + AXI Lite ×1)
// 内部调用 matmul_systolic_core 替代 matmul_core
// 测试平台 (matmul_tb.cpp / diag_tb.cpp) 可直接复用
// ============================================================================

#include "matmul_top.h"
#include "matmul_systolic.h"

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
    // AXI 接口
    #pragma HLS INTERFACE axis      port=in_A
    #pragma HLS INTERFACE axis      port=in_B
    #pragma HLS INTERFACE axis      port=out_C
    #pragma HLS INTERFACE s_axilite port=M
    #pragma HLS INTERFACE s_axilite port=N
    #pragma HLS INTERFACE s_axilite port=K
    #pragma HLS INTERFACE s_axilite port=return

    data_t A_buf[TILE_M][TILE_K];
    data_t B_buf[TILE_K][TILE_N];
    acc_t  C_buf[TILE_M][TILE_N];

    // 分区策略与 systolic core 一致
    // A_buf: K 维完全分区, 消除脉动阵列读端口冲突
    #pragma HLS ARRAY_PARTITION variable=A_buf complete dim=2
    // B_buf: K 维完全分区, 消除脉动阵列读端口冲突
    #pragma HLS ARRAY_PARTITION variable=B_buf complete dim=1
    // C_buf: N 维完全分区, 消除写回 += 端口冲突
    #pragma HLS ARRAY_PARTITION variable=C_buf complete dim=2

    int M_tiles = (M + TILE_M - 1) / TILE_M;
    int N_tiles = (N + TILE_N - 1) / TILE_N;
    int K_tiles = (K + TILE_K - 1) / TILE_K;

    for (int m_tile = 0; m_tile < M_tiles; m_tile++) {
        #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_M/TILE_M

        for (int n_tile = 0; n_tile < N_tiles; n_tile++) {
            #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_N/TILE_N

            // 清零 C_buf (新一轮 K 累加)
            ClearC:
            for (int i = 0; i < TILE_M; i++) {
                for (int j = 0; j < TILE_N; j++) {
                    #pragma HLS PIPELINE II=1
                    C_buf[i][j] = 0;
                }
            }

            // K 维度累加
            for (int k_tile = 0; k_tile < K_tiles; k_tile++) {
                #pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_K/TILE_K

                // 加载 A_tile (TILE_M × TILE_K)
                LoadA:
                for (int i = 0; i < TILE_M; i++) {
                    for (int k = 0; k < TILE_K; k++) {
                        #pragma HLS PIPELINE II=1
                        in_A >> A_buf[i][k];
                    }
                }

                // 加载 B_tile (TILE_K × TILE_N)
                LoadB:
                for (int k = 0; k < TILE_K; k++) {
                    for (int j = 0; j < TILE_N; j++) {
                        #pragma HLS PIPELINE II=1
                        in_B >> B_buf[k][j];
                    }
                }

                // 脉动阵列计算: C_buf += A_buf × B_buf
                matmul_systolic_core(A_buf, B_buf, C_buf);
            }

            // 写出 C_tile (TILE_M × TILE_N)
            StoreC:
            for (int i = 0; i < TILE_M; i++) {
                for (int j = 0; j < TILE_N; j++) {
                    #pragma HLS PIPELINE II=1
                    out_C << (data_t)C_buf[i][j];
                }
            }
        }
    }
}
