#ifndef MATMUL_TOP_H
#define MATMUL_TOP_H

#include "matmul.h"

void matmul_top(
    axis_stream_t &in_A,
    axis_stream_t &in_B,
    axis_stream_t &out_C,
    int M,
    int N,
    int K
);

#endif
