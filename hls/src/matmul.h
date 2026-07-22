#ifndef MATMUL_H
#define MATMUL_H

#include <ap_fixed.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

typedef ap_fixed<16, 8, AP_RND, AP_SAT> data_t;
typedef ap_fixed<48, 32, AP_RND, AP_SAT> acc_t;
// The board DMA path is 32 bits wide. Arithmetic remains Q8.8; the payload is
// carried in TDATA[15:0] and output values are sign-extended to 32 bits.
typedef ap_axis<32, 0, 0, 0> axis_word_t;
typedef hls::stream<axis_word_t> axis_stream_t;

#define MAX_M 64
#define MAX_N 64
#define MAX_K 64
#define TILE_M 16
#define TILE_N 16
#define TILE_K 16

static data_t axis_to_data(const axis_word_t &word) {
    data_t value;
    value.range(15, 0) = word.data.range(15, 0);
    return value;
}

static axis_word_t data_to_axis(data_t value, bool last) {
    axis_word_t word;
    ap_int<16> raw = value.range(15, 0);
    ap_int<32> extended = raw;
    word.data = extended;
    word.keep = -1;
    word.strb = -1;
    word.last = last ? 1 : 0;
    return word;
}

#endif
