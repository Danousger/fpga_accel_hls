// ============================================================================
// diag_tb.cpp — 最小诊断: 验证 matmul_top 的流协议和计算正确性
// ============================================================================

#include <cstdio>
#include <cmath>
#include "../src/matmul.h"
#include "../src/matmul_top.h"

int main() {
    printf("=== 诊断: 单 tile 矩阵乘法 (M=N=K=%d) ===\n", TILE_M);

    int M = TILE_M, N = TILE_N, K = TILE_K;

    // 简单测试数据: A[i][k] = 1.0, B[k][j] = 1.0
    // 预期: C[i][j] = K = 16.0
    data_stream in_A, in_B, out_C;

    // 发送 A: 全部填 1.0 (Q8.8: 256)
    for (int i = 0; i < TILE_M; i++)
        for (int k = 0; k < TILE_K; k++)
            in_A << data_t(1.0);

    // 发送 B: 全部填 1.0
    for (int k = 0; k < TILE_K; k++)
        for (int j = 0; j < TILE_N; j++)
            in_B << data_t(1.0);

    printf("发送: %d 个 A 元素, %d 个 B 元素\n",
           TILE_M * TILE_K, TILE_K * TILE_N);

    // 调用 DUT
    matmul_top(in_A, in_B, out_C, M, N, K);

    // 读取结果
    int errors = 0;
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            data_t val;
            out_C >> val;
            float hw = val.to_float();
            float expected = (float)K;  // K 个 1.0 相加
            float err = fabsf(hw - expected);
            if (err > 0.01f) {
                printf("  C[%d][%d] = %.4f (期望 %.4f, err=%.4f)\n",
                       i, j, hw, expected, err);
                errors++;
                if (errors > 10) break;
            }
        }
        if (errors > 10) break;
    }

    printf("检查了 %d 个元素\n", TILE_M * TILE_N);
    if (errors == 0) {
        printf("✓ 全部正确!\n");
    } else {
        printf("✗ 发现 %d 个错误\n", errors);
    }

    // --- 检查流是否还有残留数据 ---
    if (!out_C.empty()) {
        printf("✗ out_C 还有残留数据!\n");
    }
    if (!in_A.empty()) {
        printf("✗ in_A 还有未消费数据!\n");
    }
    if (!in_B.empty()) {
        printf("✗ in_B 还有未消费数据!\n");
    }

    return (errors == 0) ? 0 : 1;
}
