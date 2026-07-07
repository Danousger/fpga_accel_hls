// ============================================================================
// matmul_systolic_all.cpp — 脉动阵列版单文件 (综合用)
//
// Vitis HLS 2020.2 clang-tidy 限制:
//   1. 不解析 #include "..." → 全部代码展平到单文件
//   2. 不展开 #pragma 中的宏 → factor 用字面量 4 (非 SYSTOLIC_SIZE)
//
// 目标: Zynq-7020 (xc7z020clg400-2), Q8.8, 4×4 脉动阵列, II=1, 100MHz
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
#define SYSTOLIC_SIZE 4
#define MIN(a,b) (((a)<(b))?(a):(b))

// ========================== 脉动阵列核心 ==========================

void matmul_systolic_core(
    data_t  A_buf[TILE_M][TILE_K],
    data_t  B_buf[TILE_K][TILE_N],
    acc_t   C_buf[TILE_M][TILE_N]
) {
    // 完全分区 (complete) → 寄存器, 无端口冲突, 达成 II=1
    #pragma HLS ARRAY_PARTITION variable=A_buf complete dim=2
    #pragma HLS ARRAY_PARTITION variable=B_buf complete dim=1
    #pragma HLS ARRAY_PARTITION variable=C_buf complete dim=2

    for (int bi = 0; bi < TILE_M; bi += 4) {
        for (int bj = 0; bj < TILE_N; bj += 4) {

            acc_t  c_reg[4][4];
            data_t a_pipe[4][4];
            data_t b_pipe[4][4];

            #pragma HLS ARRAY_PARTITION variable=c_reg  complete
            #pragma HLS ARRAY_PARTITION variable=a_pipe complete
            #pragma HLS ARRAY_PARTITION variable=b_pipe complete

            // 初始化: 从 C_buf 读入已有部分和, 流水线寄存器清零
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    #pragma HLS UNROLL
                    c_reg[i][j]  = C_buf[bi + i][bj + j];
                    a_pipe[i][j] = 0;
                    b_pipe[i][j] = 0;
                }
            }

            // 脉动计算: TILE_K + 2*(S-1) = 16 + 6 = 22 周期
            for (int t = 0; t < TILE_K + 6; t++) {
                #pragma HLS PIPELINE II=1

                for (int i = 3; i >= 0; i--) {
                    for (int j = 3; j >= 0; j--) {
                        #pragma HLS UNROLL

                        data_t a_val;
                        if (j == 0) {
                            int k_a = t - i;
                            a_val = (k_a >= 0 && k_a < TILE_K)
                                  ? A_buf[bi + i][k_a] : (data_t)0;
                        } else {
                            a_val = a_pipe[i][j - 1];
                        }

                        data_t b_val;
                        if (i == 0) {
                            int k_b = t - j;
                            b_val = (k_b >= 0 && k_b < TILE_K)
                                  ? B_buf[k_b][bj + j] : (data_t)0;
                        } else {
                            b_val = b_pipe[i - 1][j];
                        }

                        c_reg[i][j] += (acc_t)a_val * (acc_t)b_val;

                        a_pipe[i][j] = a_val;
                        b_pipe[i][j] = b_val;
                    }
                }
            }

            // 写回 C_buf (覆盖, c_reg 已含累加结果)
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    #pragma HLS UNROLL
                    C_buf[bi + i][bj + j] = c_reg[i][j];
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

    #pragma HLS ARRAY_PARTITION variable=A_buf complete dim=2
    #pragma HLS ARRAY_PARTITION variable=B_buf complete dim=1
    #pragma HLS ARRAY_PARTITION variable=C_buf complete dim=2

    int M_tiles = (M + 15) / 16;
    int N_tiles = (N + 15) / 16;
    int K_tiles = (K + 15) / 16;

    for (int m_tile = 0; m_tile < M_tiles; m_tile++) {
        for (int n_tile = 0; n_tile < N_tiles; n_tile++) {

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
                // 脉动阵列计算
                matmul_systolic_core(A_buf, B_buf, C_buf);
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
