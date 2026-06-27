// ============================================================================
// matmul_tb.cpp — 矩阵乘法加速器 C 仿真测试平台
//
// Q8.8 定点格式:
//   - 输入范围: [-128, 128), 精度 1/256
//   - 输出范围: [-128, 128), 累加器 48bit 防溢出
//   - 测试数据需保证结果在 [-128, 128) 范围内
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "matmul.h"
#include "matmul_top.h"

// ---------------------------------------------------------------------------
// 浮点参考实现
// ---------------------------------------------------------------------------
void matmul_golden(float A[], float B[], float C[], int M, int N, int K) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++) {
                sum += A[i * K + k] * B[k * N + j];
            }
            C[i * N + j] = sum;
        }
    }
}

// ---------------------------------------------------------------------------
// 定点 ↔ 浮点转换
// ---------------------------------------------------------------------------
inline float f2f(data_t v)  { return v.to_float(); }
inline data_t f2q(float v)  { return data_t(v); }
inline float a2f(acc_t v)   { return v.to_float(); }

// ---------------------------------------------------------------------------
// 按 matmul_top 的流协议发送分块数据并调用 DUT
//
// 协议:
//   for m_tile in M_tiles:
//     for n_tile in N_tiles:
//       for k_tile in K_tiles:
//         write A_tile (TILE_M × TILE_K) → in_A
//         write B_tile (TILE_K × TILE_N) → in_B
//       read  C_tile (TILE_M × TILE_N) ← out_C
// ---------------------------------------------------------------------------
void run_dut(
    float A[], int M, int K,
    float B[], int N,
    float C[],         // 输出
    int   &pass_cnt,
    int   &fail_cnt
) {
    int M_tiles = (M + TILE_M - 1) / TILE_M;
    int N_tiles = (N + TILE_N - 1) / TILE_N;
    int K_tiles = (K + TILE_K - 1) / TILE_K;

    data_stream in_A, in_B, out_C;

    // --- 发送分块数据 ---
    for (int m_tile = 0; m_tile < M_tiles; m_tile++) {
        int m_act = MIN(TILE_M, M - m_tile * TILE_M);

        for (int n_tile = 0; n_tile < N_tiles; n_tile++) {
            int n_act = MIN(TILE_N, N - n_tile * TILE_N);

            for (int k_tile = 0; k_tile < K_tiles; k_tile++) {
                int k_act = MIN(TILE_K, K - k_tile * TILE_K);

                // A tile (行主序, 零填充边界)
                for (int i = 0; i < TILE_M; i++) {
                    for (int k = 0; k < TILE_K; k++) {
                        float val = 0.0f;
                        if (i < m_act && k < k_act)
                            val = A[(m_tile * TILE_M + i) * K +
                                    (k_tile * TILE_K + k)];
                        in_A << f2q(val);
                    }
                }

                // B tile (行主序, 零填充边界)
                for (int k = 0; k < TILE_K; k++) {
                    for (int j = 0; j < TILE_N; j++) {
                        float val = 0.0f;
                        if (k < k_act && j < n_act)
                            val = B[(k_tile * TILE_K + k) * N +
                                    (n_tile * TILE_N + j)];
                        in_B << f2q(val);
                    }
                }
            } // k_tile

        } // n_tile
    } // m_tile

    // --- 调用 DUT ---
    matmul_top(in_A, in_B, out_C, M, N, K);

    // --- 接收并验证结果 ---
    float C_golden[MAX_M * MAX_N];
    matmul_golden(A, B, C_golden, M, N, K);

    int local_pass = 0, local_fail = 0;
    float max_err = 0.0f;

    for (int m_tile = 0; m_tile < M_tiles; m_tile++) {
        int m_act = MIN(TILE_M, M - m_tile * TILE_M);

        for (int n_tile = 0; n_tile < N_tiles; n_tile++) {
            int n_act = MIN(TILE_N, N - n_tile * TILE_N);

            for (int i = 0; i < TILE_M; i++) {
                for (int j = 0; j < TILE_N; j++) {
                    data_t val;
                    out_C >> val;
                    float hw = f2f(val);

                    if (i < m_act && j < n_act) {
                        int row = m_tile * TILE_M + i;
                        int col = n_tile * TILE_N + j;
                        float sw = C_golden[row * N + col];
                        C[row * N + col] = hw;
                        float err = fabsf(hw - sw);
                        if (err > max_err) max_err = err;

                        // Q8.8 精度: 1 LSB = 0.004, K次累加允许多 LSB 误差
                        // 理论最大舍入误差 ≈ K × 0.5 LSB, 这里放宽容差到 8 LSB
                        if (err > 0.03f) {
                            if (local_fail < 5) {
                                printf("  ✗ C[%d][%d]: HW=%.4f, SW=%.4f, err=%.4f\n",
                                       row, col, hw, sw, err);
                            }
                            local_fail++;
                        } else {
                            local_pass++;
                        }
                    }
                }
            }
        }
    }

    printf("  最大误差: %.6f, 通过: %d, 失败: %d\n",
           max_err, local_pass, local_fail);
    pass_cnt += local_pass;
    fail_cnt += local_fail;
}

// ---------------------------------------------------------------------------
// 单测试用例
// ---------------------------------------------------------------------------
void run_test(const char *name, int M, int N, int K,
              float scale, int &pass, int &fail)
{
    printf("\n── %s (M=%d, N=%d, K=%d) ──\n", name, M, N, K);

    // 分配测试矩阵 (用一维数组, 行主序)
    float *A = new float[M * K];
    float *B = new float[K * N];
    float *C = new float[M * N];

    // 填充数据: 范围 [-scale, +scale]
    // scale 的选择: K × scale² < 128 以保证输出不饱和
    srand(42);
    for (int i = 0; i < M * K; i++)
        A[i] = ((float)(rand() % 8192) / 4096.0f - 1.0f) * scale;
    for (int i = 0; i < K * N; i++)
        B[i] = ((float)(rand() % 8192) / 4096.0f - 1.0f) * scale;

    run_dut(A, M, K, B, N, C, pass, fail);

    delete[] A;
    delete[] B;
    delete[] C;
}

// ---------------------------------------------------------------------------
// 主入口
// ---------------------------------------------------------------------------
int main() {
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║  定点数矩阵乘法加速器 — C 仿真测试套件           ║\n");
    printf("║  data_t: ap_fixed<16,8> (Q8.8, 范围 ±128)       ║\n");
    printf("║  acc_t:  ap_fixed<48,32> (48bit, 防溢出)         ║\n");
    printf("║  Tile: %d×%d×%d, 并行度: %d MAC/cycle             ║\n",
           TILE_M, TILE_N, TILE_K, PARALLEL_N);
    printf("╚══════════════════════════════════════════════════╝\n");

    int total_pass = 0, total_fail = 0;

    // 测试 1: 单 tile (M=N=K=16), 小值域
    run_test("单 Tile 基准", 16, 16, 16, 2.0f, total_pass, total_fail);

    // 测试 2: 非对齐尺寸 (触发边界填充)
    run_test("非对齐尺寸", 13, 17, 11, 2.0f, total_pass, total_fail);

    // 测试 3: 中等矩阵
    run_test("中等矩阵 48×48", 48, 48, 48, 1.0f, total_pass, total_fail);

    // 测试 4: 矩形矩阵
    run_test("矩形 8×32×16", 8, 32, 16, 2.0f, total_pass, total_fail);

    // 测试 5: 最大尺寸压力
    run_test("最大尺寸 64×64", 64, 64, 64, 0.5f, total_pass, total_fail);

    printf("\n══════════════════════════════════════════\n");
    printf("  总计: 通过 %d, 失败 %d\n", total_pass, total_fail);
    if (total_fail == 0)
        printf("  ✓ 全部通过!\n");
    else
        printf("  ✗ 存在 %d 个失败\n", total_fail);
    printf("══════════════════════════════════════════\n");

    return (total_fail == 0) ? 0 : 1;
}
