#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "matmul_top.h"

static axis_word_t packet(float value, bool last) {
    return data_to_axis(data_t(value), last);
}

static float unpack(const axis_word_t &word) {
    return axis_to_data(word).to_float();
}

static bool run_case(int M, int N, int K) {
    std::vector<float> A(M * K), B(K * N), golden(M * N, 0.0f);
    srand(M * 10000 + N * 100 + K);
    for (size_t i = 0; i < A.size(); ++i) A[i] = ((rand() % 1024) - 512) / 512.0f;
    for (size_t i = 0; i < B.size(); ++i) B[i] = ((rand() % 1024) - 512) / 512.0f;

    for (int i = 0; i < M; ++i)
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < K; ++k)
                golden[i * N + j] += A[i * K + k] * B[k * N + j];

    axis_stream_t in_A, in_B, out_C;
    const int m_tiles = (M + 15) / 16;
    const int n_tiles = (N + 15) / 16;
    const int k_tiles = (K + 15) / 16;
    for (int mt = 0; mt < m_tiles; ++mt) {
        for (int nt = 0; nt < n_tiles; ++nt) {
            for (int kt = 0; kt < k_tiles; ++kt) {
                for (int i = 0; i < 16; ++i) for (int k = 0; k < 16; ++k) {
                    int r = mt * 16 + i, c = kt * 16 + k;
                    float v = (r < M && c < K) ? A[r * K + c] : 0.0f;
                    in_A.write(packet(v, i == 15 && k == 15));
                }
                for (int k = 0; k < 16; ++k) for (int j = 0; j < 16; ++j) {
                    int r = kt * 16 + k, c = nt * 16 + j;
                    float v = (r < K && c < N) ? B[r * N + c] : 0.0f;
                    in_B.write(packet(v, k == 15 && j == 15));
                }
            }
        }
    }

    matmul_top(in_A, in_B, out_C, M, N, K);
    float max_error = 0.0f;
    bool framing_ok = true;
    bool width_ok = true;
    for (int mt = 0; mt < m_tiles; ++mt) for (int nt = 0; nt < n_tiles; ++nt) {
        for (int i = 0; i < 16; ++i) for (int j = 0; j < 16; ++j) {
            axis_word_t word = out_C.read();
            bool expected_last = i == 15 && j == 15;
            framing_ok &= ((bool)word.last == expected_last);
            ap_int<16> low = word.data.range(15, 0);
            ap_int<32> expected_data = low;
            width_ok &= (word.data.range(31, 0) == expected_data.range(31, 0));
            width_ok &= (word.keep == 0xF) && (word.strb == 0xF);
            int r = mt * 16 + i, c = nt * 16 + j;
            if (r < M && c < N)
                max_error = fmaxf(max_error, fabsf(unpack(word) - golden[r * N + c]));
        }
    }
    printf("M=%d N=%d K=%d max_error=%.6f framing=%s width=%s\n",
           M, N, K, max_error, framing_ok ? "PASS" : "FAIL",
           width_ok ? "PASS" : "FAIL");
    return framing_ok && width_ok && max_error < 0.04f;
}

int main() {
    bool ok = true;
    ok &= run_case(16, 16, 16);
    ok &= run_case(13, 17, 11);
    ok &= run_case(32, 32, 32);
    ok &= run_case(64, 64, 64);
    return ok ? 0 : 1;
}
