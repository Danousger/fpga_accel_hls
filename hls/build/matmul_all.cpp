// ============================================================================
// matmul_all.cpp — 定点数矩阵乘法加速器 (单文件版, 综合用)
// 目标: Zynq-7020 (xc7z020clg400-2), Q8.8 定点, 4 MAC 并行
// ============================================================================

#include <ap_fixed.h>
#include <hls_stream.h>

// ========================== 类型 & 常量 ==========================

typedef ap_fixed<16, 8, AP_RND, AP_SAT>  data_t;
typedef ap_fixed<48, 32, AP_RND, AP_SAT> acc_t;
typedef hls::stream<data_t> data_stream;

#define MAX_M   64
#define MAX_N   64
#define MAX_K   64
#define TILE_M  16
#define TILE_N  16
#define TILE_K  16
#define PARALLEL_N  4
#define MIN(a,b) (((a)<(b))?(a):(b))

// ========================== 核心计算 ==========================

void matmul_core(
    data_t  A_buf[TILE_M][TILE_K],
    data_t  B_buf[TILE_K][TILE_N],
    acc_t   C_buf[TILE_M][TILE_N]
) {
    #pragma HLS ARRAY_PARTITION variable=A_buf cyclic factor=4 dim=2
    #pragma HLS ARRAY_PARTITION variable=B_buf cyclic factor=4 dim=2
    #pragma HLS ARRAY_PARTITION variable=C_buf cyclic factor=4 dim=2

    for (int i = 0; i < TILE_M; i++) {
        for (int j = 0; j < TILE_N; j += PARALLEL_N) {
            for (int k = 0; k < TILE_K; k++) {
                #pragma HLS PIPELINE
                data_t a_val = A_buf[i][k];
                #pragma HLS UNROLL
                for (int p = 0; p < PARALLEL_N; p++) {
                    data_t b_val = B_buf[k][j + p];
                    acc_t  product = (acc_t)a_val * (acc_t)b_val;
                    C_buf[i][j + p] += product;
                }
            }
        }
    }
}

// ========================== 顶层模块 ==========================

void matmul_top(
    data_stream &in_A,
    data_stream &in_B,
    data_stream &out_C,
    int          M,
    int          N,
    int          K
) {
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

    #pragma HLS ARRAY_PARTITION variable=A_buf cyclic factor=4 dim=2
    #pragma HLS ARRAY_PARTITION variable=B_buf cyclic factor=4 dim=2
    #pragma HLS ARRAY_PARTITION variable=C_buf cyclic factor=4 dim=2

    int M_tiles = (M + 15) / 16;
    int N_tiles = (N + 15) / 16;
    int K_tiles = (K + 15) / 16;

    for (int m_tile = 0; m_tile < M_tiles; m_tile++) {
        int m_actual = (M - m_tile * 16 < 16) ? (M - m_tile * 16) : 16;
        for (int n_tile = 0; n_tile < N_tiles; n_tile++) {
            int n_actual = (N - n_tile * 16 < 16) ? (N - n_tile * 16) : 16;

            // 清零 C_buf
            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    #pragma HLS PIPELINE II=1
                    C_buf[i][j] = 0;
                }
            }

            for (int k_tile = 0; k_tile < K_tiles; k_tile++) {
                // 加载 A_tile
                for (int i = 0; i < 16; i++) {
                    for (int k = 0; k < 16; k++) {
                        #pragma HLS PIPELINE II=1
                        in_A >> A_buf[i][k];
                    }
                }
                // 加载 B_tile
                for (int k = 0; k < 16; k++) {
                    for (int j = 0; j < 16; j++) {
                        #pragma HLS PIPELINE II=1
                        in_B >> B_buf[k][j];
                    }
                }
                // 计算
                matmul_core(A_buf, B_buf, C_buf);
            }

            // 写出 C_tile
            for (int i = 0; i < 16; i++) {
                for (int j = 0; j < 16; j++) {
                    #pragma HLS PIPELINE II=1
                    out_C << (data_t)C_buf[i][j];
                }
            }
        }
    }
}
