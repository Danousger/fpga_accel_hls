#ifndef MATMUL_SYSTOLIC_H
#define MATMUL_SYSTOLIC_H

#include "matmul.h"

#define SYSTOLIC_SIZE 4

void matmul_systolic_core(
    data_t A_buf[TILE_M][TILE_K],
    data_t B_buf[TILE_K][TILE_N],
    acc_t C_buf[TILE_M][TILE_N]
);

#endif
