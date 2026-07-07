// ============================================================================
// matmul_systolic_core.cpp — 输出驻留 2D 脉动阵列核心
//
// 架构 (SYSTOLIC_SIZE=4, 4×4 PE 阵列):
//
//        B[k][j] ↓   ↓   ↓   ↓
//              ┌───┬───┬───┬───┐
//   A[i][k] →  │PE │PE │PE │PE │  → A 流出 (丢弃)
//              ├───┼───┼───┼───┤
//      →       │PE │PE │PE │PE │  →
//              ├───┼───┼───┼───┤
//      →       │PE │PE │PE │PE │  →
//              ├───┼───┼───┼───┤
//      →       │PE │PE │PE │PE │  →
//              └───┴───┴───┴───┘
//               ↓   ↓   ↓   ↓     B 流出 (丢弃)
//
//   每个 PE(i,j):
//     - 从左侧接收 A, 从上方接收 B
//     - 计算 c_reg[i][j] += A × B
//     - 将 A 传给右侧 PE, B 传给下方 PE
//     - c_reg 是寄存器 (非 BRAM) → 无 RAW 依赖 → II=1
//
// 时序 (输入偏斜):
//   - A[i][k] 在周期 k+i 进入 PE(i,0)   (行 i 延迟 i 周期)
//   - B[k][j] 在周期 k+j 进入 PE(0,j)   (列 j 延迟 j 周期)
//   - A 经 j 周期到达 PE(i,j), B 经 i 周期到达 PE(i,j)
//   - 两者在周期 k+i+j 相遇于 PE(i,j) → 正确配对
//
// 每个 4×4 输出块: TILE_K + 2*(S-1) = 22 周期
// 16×16 tile 分为 16 个 4×4 块 → 352 计算周期
// ============================================================================

#include "matmul_systolic.h"

void matmul_systolic_core(
    data_t  A_buf[TILE_M][TILE_K],
    data_t  B_buf[TILE_K][TILE_N],
    acc_t   C_buf[TILE_M][TILE_N]
) {
    const int S = SYSTOLIC_SIZE;

    // A_buf: K 维 (dim=2) 完全分区 → 每元素独立寄存器, 无端口冲突
    #pragma HLS ARRAY_PARTITION variable=A_buf complete dim=2
    // B_buf: K 维 (dim=1) 完全分区 → 每元素独立寄存器, 无端口冲突
    #pragma HLS ARRAY_PARTITION variable=B_buf complete dim=1
    // C_buf: N 维 (dim=2) 完全分区 → 写回时无端口冲突 (+= 读-改-写)
    #pragma HLS ARRAY_PARTITION variable=C_buf complete dim=2

    // 将 16×16 输出划分为 S×S 子块, 逐块用脉动阵列计算
    for (int bi = 0; bi < TILE_M; bi += S) {
        for (int bj = 0; bj < TILE_N; bj += S) {

            // PE 累加器 (寄存器, complete 分区 → 每 PE 独立寄存器)
            acc_t  c_reg[S][S];
            // A 水平流水线寄存器: a_pipe[i][j] = PE(i,j) 当前持有的 A 值
            data_t a_pipe[S][S];
            // B 垂直流水线寄存器: b_pipe[i][j] = PE(i,j) 当前持有的 B 值
            data_t b_pipe[S][S];

            #pragma HLS ARRAY_PARTITION variable=c_reg  complete
            #pragma HLS ARRAY_PARTITION variable=a_pipe complete
            #pragma HLS ARRAY_PARTITION variable=b_pipe complete

            // 初始化: 从 C_buf 读入已有部分和 (跨 k_tile 累加)
            // 流水线寄存器清零
            Init:
            for (int i = 0; i < S; i++) {
                for (int j = 0; j < S; j++) {
                    #pragma HLS UNROLL
                    c_reg[i][j]  = C_buf[bi + i][bj + j];
                    a_pipe[i][j] = 0;
                    b_pipe[i][j] = 0;
                }
            }

            // 脉动计算主循环
            // 总周期 = TILE_K + 2*(S-1) (K 次有效 MAC + S-1 行偏斜 + S-1 列偏斜)
            Systolic:
            for (int t = 0; t < TILE_K + 2 * (S - 1); t++) {
                #pragma HLS PIPELINE II=1

                // 展开所有 S×S 个 PE (同周期并行执行)
                // 逆序迭代 (i=S-1→0, j=S-1→0): 每个 PE 在上游邻居写入前读取
                // 其流水线寄存器, 匹配硬件寄存器 "时钟沿更新" 语义
                // (C-sim 顺序执行, 若正序则 PE(i,j) 会读到同周期 PE(i,j-1) 的新值)
                for (int i = S - 1; i >= 0; i--) {
                    for (int j = S - 1; j >= 0; j--) {
                        #pragma HLS UNROLL

                        // --- A 数据: 从左邻 PE 或阵列左边界输入 ---
                        data_t a_val;
                        if (j == 0) {
                            // 左边界: 从 A_buf 读取, 行 i 偏迟 i 周期
                            int k_a = t - i;
                            a_val = (k_a >= 0 && k_a < TILE_K)
                                  ? A_buf[bi + i][k_a]
                                  : (data_t)0;
                        } else {
                            // 内部: 从左邻 PE 的流水线寄存器读取 (上一周期值)
                            a_val = a_pipe[i][j - 1];
                        }

                        // --- B 数据: 从上邻 PE 或阵列上边界输入 ---
                        data_t b_val;
                        if (i == 0) {
                            // 上边界: 从 B_buf 读取, 列 j 偏迟 j 周期
                            int k_b = t - j;
                            b_val = (k_b >= 0 && k_b < TILE_K)
                                  ? B_buf[k_b][bj + j]
                                  : (data_t)0;
                        } else {
                            // 内部: 从上邻 PE 的流水线寄存器读取 (上一周期值)
                            b_val = b_pipe[i - 1][j];
                        }

                        // --- MAC: 累加器在寄存器中, 无 BRAM 依赖 ---
                        // 偏斜区域外 a_val/b_val 为 0, 乘积为 0, 不影响累加结果
                        c_reg[i][j] += (acc_t)a_val * (acc_t)b_val;

                        // --- 传播: 写入流水线寄存器供下周期使用 ---
                        a_pipe[i][j] = a_val;
                        b_pipe[i][j] = b_val;
                    }
                }
            }

            // 写回: 覆盖 C_buf (c_reg 已包含累加结果, 无需 +=)
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
