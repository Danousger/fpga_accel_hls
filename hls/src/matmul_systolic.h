// ============================================================================
// matmul_systolic.h — Systolic Array 核心头文件
//
// 输出驻留 (Output-Stationary) 2D 脉动阵列:
//   - A 从左向右流动, B 从上向下流动
//   - 每个 PE 用寄存器存储累加器 (消除 BRAM RAW 依赖 → II=1)
//   - 输入数据带偏斜 (skew): A 行 i 延迟 i 周期, B 列 j 延迟 j 周期
//
// SYSTOLIC_SIZE×SYSTOLIC_SIZE 个 PE, 每周期做 SYSTOLIC_SIZE² 次 MAC
// ============================================================================

#ifndef MATMUL_SYSTOLIC_H
#define MATMUL_SYSTOLIC_H

#include "matmul.h"

// 脉动阵列边长 (S×S 个 PE)
// S=4 → 16 PEs, ~32 DSP48E, 16 MAC/cycle @ II=1
#define SYSTOLIC_SIZE 4

// 脉动阵列核心: C_buf += A_buf × B_buf (单 tile 16×16×16)
void matmul_systolic_core(
    data_t  A_buf[TILE_M][TILE_K],
    data_t  B_buf[TILE_K][TILE_N],
    acc_t   C_buf[TILE_M][TILE_N]
);

#endif // MATMUL_SYSTOLIC_H
