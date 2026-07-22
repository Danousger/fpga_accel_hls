#include "matmul_top.h"
#include "matmul_systolic.h"

void matmul_top(
    axis_stream_t &in_A,
    axis_stream_t &in_B,
    axis_stream_t &out_C,
    int M,
    int N,
    int K
) {
    #pragma HLS INTERFACE axis register both port=in_A
    #pragma HLS INTERFACE axis register both port=in_B
    #pragma HLS INTERFACE axis register both port=out_C
    #pragma HLS INTERFACE s_axilite port=M bundle=control
    #pragma HLS INTERFACE s_axilite port=N bundle=control
    #pragma HLS INTERFACE s_axilite port=K bundle=control
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    data_t A_buf[TILE_M][TILE_K];
    data_t B_buf[TILE_K][TILE_N];
    acc_t C_buf[TILE_M][TILE_N];

    #pragma HLS ARRAY_PARTITION variable=A_buf complete dim=2
    #pragma HLS ARRAY_PARTITION variable=B_buf complete dim=1
    #pragma HLS ARRAY_PARTITION variable=C_buf complete dim=2

    const int M_tiles = (M + TILE_M - 1) / TILE_M;
    const int N_tiles = (N + TILE_N - 1) / TILE_N;
    const int K_tiles = (K + TILE_K - 1) / TILE_K;

    for (int mt = 0; mt < M_tiles; ++mt) {
        #pragma HLS LOOP_TRIPCOUNT min=1 max=4
        for (int nt = 0; nt < N_tiles; ++nt) {
            #pragma HLS LOOP_TRIPCOUNT min=1 max=4
            for (int i = 0; i < TILE_M; ++i) {
                for (int j = 0; j < TILE_N; ++j) {
                    #pragma HLS PIPELINE II=1
                    C_buf[i][j] = 0;
                }
            }

            for (int kt = 0; kt < K_tiles; ++kt) {
                #pragma HLS LOOP_TRIPCOUNT min=1 max=4
                for (int i = 0; i < TILE_M; ++i) {
                    for (int k = 0; k < TILE_K; ++k) {
                        #pragma HLS PIPELINE II=1
                        A_buf[i][k] = axis_to_data(in_A.read());
                    }
                }
                for (int k = 0; k < TILE_K; ++k) {
                    for (int j = 0; j < TILE_N; ++j) {
                        #pragma HLS PIPELINE II=1
                        B_buf[k][j] = axis_to_data(in_B.read());
                    }
                }
                matmul_systolic_core(A_buf, B_buf, C_buf);
            }

            for (int i = 0; i < TILE_M; ++i) {
                for (int j = 0; j < TILE_N; ++j) {
                    #pragma HLS PIPELINE II=1
                    const bool last = (i == TILE_M - 1) && (j == TILE_N - 1);
                    out_C.write(data_to_axis((data_t)C_buf[i][j], last));
                }
            }
        }
    }
}
