
#include "matmul_systolic.h"

void matmul_systolic_core(
    data_t  A_buf[TILE_M][TILE_K],
    data_t  B_buf[TILE_K][TILE_N],
    acc_t   C_buf[TILE_M][TILE_N]
) {
    const int S = SYSTOLIC_SIZE;

    #pragma HLS ARRAY_PARTITION variable=A_buf complete dim=2

    #pragma HLS ARRAY_PARTITION variable=B_buf complete dim=1

    #pragma HLS ARRAY_PARTITION variable=C_buf complete dim=2


    for (int bi = 0; bi < TILE_M; bi += S) {
        for (int bj = 0; bj < TILE_N; bj += S) {
            acc_t  c_reg[S][S];

            data_t a_pipe[S][S];

            data_t b_pipe[S][S];

            #pragma HLS ARRAY_PARTITION variable=c_reg  complete
            #pragma HLS ARRAY_PARTITION variable=a_pipe complete
            #pragma HLS ARRAY_PARTITION variable=b_pipe complete

            Init:
            for (int i = 0; i < S; i++) {
                for (int j = 0; j < S; j++) {
                    #pragma HLS UNROLL
                    c_reg[i][j]  = C_buf[bi + i][bj + j];
                    a_pipe[i][j] = 0;
                    b_pipe[i][j] = 0;
                }
            }
            Systolic:
            for (int t = 0; t < TILE_K + 2 * (S - 1); t++) {
                #pragma HLS PIPELINE II=1
                for (int i = S - 1; i >= 0; i--) {
                    for (int j = S - 1; j >= 0; j--) {
                        #pragma HLS UNROLL

                        data_t a_val;
                        if (j == 0) {

                            int k_a = t - i;
                            a_val = (k_a >= 0 && k_a < TILE_K)
                                  ? A_buf[bi + i][k_a]
                                  : (data_t)0;
                        } else {

                            a_val = a_pipe[i][j - 1];
                        }


                        data_t b_val;
                        if (i == 0) {

                            int k_b = t - j;
                            b_val = (k_b >= 0 && k_b < TILE_K)
                                  ? B_buf[k_b][bj + j]
                                  : (data_t)0;
                        } else {

                            b_val = b_pipe[i - 1][j];
                        }

                        c_reg[i][j] += (acc_t)a_val * (acc_t)b_val;

                        a_pipe[i][j] = a_val;
                        b_pipe[i][j] = b_val;
                    }
                }
            }
            Writeback:
            for (int i = 0; i < S; i++) {
                for (int j = 0; j < S; j++) {
                    #pragma HLS UNROLL
                    C_buf[bi + i][bj + j] = c_reg[i][j];
                }
            }
        }
    }
}
