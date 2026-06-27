/**
 * ============================================================================
 * baremetal_driver.c — 裸机 C 语言 DMA 驱动
 *
 * 适用场景: Zynq-7020 裸机 (Standalone) 或 FreeRTOS 环境
 * 编译工具: Xilinx Vitis (原 SDK)
 *
 * 依赖:
 *   - xaxidma.h       (Xilinx AXI DMA 驱动)
 *   - xil_cache.h     (Cache 管理)
 *   - xil_printf.h    (调试输出)
 *
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "xaxidma.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "sleep.h"
#include "xtime_l.h"

// ============================================================================
// 配置常量
// ============================================================================

// DMA 设备 ID (从 xparameters.h 获取, Block Design 自动生成)
#define DMA_DEV_ID      XPAR_AXIDMA_0_DEVICE_ID

// 矩阵维度限制 (必须与 HLS 代码一致)
#define MAX_M           64
#define MAX_N           64
#define MAX_K           64
#define TILE_M          16
#define TILE_N          16
#define TILE_K          16

// 加速器基地址 (从 xparameters.h 获取)
#define ACCEL_BASEADDR  XPAR_MATMUL_TOP_0_S_AXI_CONTROL_BASEADDR
#define ACCEL_ADDR_M    (ACCEL_BASEADDR + 0x08)  // M 寄存器 (byte offset)
#define ACCEL_ADDR_N    (ACCEL_BASEADDR + 0x0C)  // N 寄存器
#define ACCEL_ADDR_K    (ACCEL_BASEADDR + 0x10)  // K 寄存器

// AXI Lite 控制寄存器偏移
#define CTRL_REG        (ACCEL_BASEADDR + 0x00)
#define STATUS_REG      (ACCEL_BASEADDR + 0x04)

// Q8.8 定点数
typedef int16_t q8_8_t;
typedef int32_t q8_8_acc_t;

#define FLOAT_TO_Q8_8(f)  ((q8_8_t)((f) * 256.0f))
#define Q8_8_TO_FLOAT(q)  (((float)(q)) / 256.0f)

// ============================================================================
// DMA 实例
// ============================================================================
static XAxiDma     dma_inst;
static XAxiDma_Config *dma_cfg;

// ============================================================================
// 初始化 DMA
// ============================================================================
int dma_init(void) {
    int status;

    dma_cfg = XAxiDma_LookupConfig(DMA_DEV_ID);
    if (!dma_cfg) {
        xil_printf("[ERROR] 找不到 DMA 配置 ID=%d\r\n", DMA_DEV_ID);
        return XST_FAILURE;
    }

    status = XAxiDma_CfgInitialize(&dma_inst, dma_cfg);
    if (status != XST_SUCCESS) {
        xil_printf("[ERROR] DMA 初始化失败: %d\r\n", status);
        return XST_FAILURE;
    }

    // 检查 DMA 是否复位正常
    if (!XAxiDma_HasSg(&dma_inst)) {
        xil_printf("[INFO] DMA 为 Simple 模式 (非 Scatter-Gather)\r\n");
    }

    // 禁用中断 (使用轮询模式)
    XAxiDma_IntrDisable(&dma_inst,
        XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(&dma_inst,
        XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

    xil_printf("[INFO] DMA 初始化成功\r\n");
    return XST_SUCCESS;
}

// ============================================================================
// 发送矩阵 A 的一个 tile (DMA MM2S)
// ============================================================================
int send_tile_A(q8_8_t *buffer, int m_actual, int k_actual) {
    int status;
    int bytes = TILE_M * TILE_K * sizeof(q8_8_t);

    // 刷新 cache 确保数据在 DDR 中
    Xil_DCacheFlushRange((UINTPTR)buffer, bytes);

    status = XAxiDma_SimpleTransfer(
        &dma_inst, (UINTPTR)buffer, bytes, XAXIDMA_DMA_TO_DEVICE);
    if (status != XST_SUCCESS) {
        xil_printf("[ERROR] DMA MM2S 发送失败: %d\r\n", status);
        return XST_FAILURE;
    }

    // 等待传输完成
    while (XAxiDma_Busy(&dma_inst, XAXIDMA_DMA_TO_DEVICE)) {
        /* 轮询 */
    }

    return XST_SUCCESS;
}

// ============================================================================
// 发送矩阵 B 的一个 tile (DMA MM2S)
// ============================================================================
int send_tile_B(q8_8_t *buffer, int k_actual, int n_actual) {
    return send_tile_A(buffer, k_actual, n_actual);  // 复用同一逻辑
}

// ============================================================================
// 接收矩阵 C 的一个 tile (DMA S2MM)
// ============================================================================
int recv_tile_C(q8_8_acc_t *buffer, int m_actual, int n_actual) {
    int status;
    int bytes = TILE_M * TILE_N * sizeof(q8_8_acc_t);

    // 失效 cache 以便读取 DMA 写入的数据
    Xil_DCacheInvalidateRange((UINTPTR)buffer, bytes);

    status = XAxiDma_SimpleTransfer(
        &dma_inst, (UINTPTR)buffer, bytes, XAXIDMA_DEVICE_TO_DMA);
    if (status != XST_SUCCESS) {
        xil_printf("[ERROR] DMA S2MM 接收失败: %d\r\n", status);
        return XST_FAILURE;
    }

    while (XAxiDma_Busy(&dma_inst, XAXIDMA_DEVICE_TO_DMA)) {
        /* 轮询 */
    }

    return XST_SUCCESS;
}

// ============================================================================
// 配置加速器参数 (通过 AXI Lite)
// ============================================================================
void accel_config(int M, int N, int K) {
    // 写入矩阵维度
    Xil_Out32(ACCEL_ADDR_M, M);
    Xil_Out32(ACCEL_ADDR_N, N);
    Xil_Out32(ACCEL_ADDR_K, K);
}

// ============================================================================
// 硬件矩阵乘法
// ============================================================================
int matmul_hw(
    float *A_float, int M, int K,
    float *B_float, int K_in, int N,
    float *C_float
) {
    if (K != K_in) {
        xil_printf("[ERROR] 矩阵维度不匹配: K=%d vs K_in=%d\r\n", K, K_in);
        return XST_FAILURE;
    }
    if (M > MAX_M || N > MAX_N || K > MAX_K) {
        xil_printf("[ERROR] 矩阵超出最大尺寸: (%d,%d,%d) vs (%d,%d,%d)\r\n",
                   M, N, K, MAX_M, MAX_N, MAX_K);
        return XST_FAILURE;
    }

    // --- 1. 量化 float → Q8.8 ---
    // (在实际系统中, 这些 buffer 应该从 DDR 分配并 cache-aligned)
    static q8_8_t     A_fixed[MAX_M * MAX_K];
    static q8_8_t     B_fixed[MAX_K * MAX_N];
    static q8_8_acc_t C_fixed[MAX_M * MAX_N];

    for (int i = 0; i < M * K; i++)
        A_fixed[i] = FLOAT_TO_Q8_8(A_float[i]);
    for (int i = 0; i < K * N; i++)
        B_fixed[i] = FLOAT_TO_Q8_8(B_float[i]);

    // --- 2. 发送 tile 数据 ---
    int M_tiles = (M + TILE_M - 1) / TILE_M;
    int N_tiles = (N + TILE_N - 1) / TILE_N;
    int K_tiles = (K + TILE_K - 1) / TILE_K;

    // tile buffer (DMA 需要物理连续内存)
    static q8_8_t     a_tile[TILE_M * TILE_K];
    static q8_8_t     b_tile[TILE_K * TILE_N];
    static q8_8_acc_t c_tile[TILE_M * TILE_N];

    for (int m_tile = 0; m_tile < M_tiles; m_tile++) {
        int m_start = m_tile * TILE_M;
        int m_act   = (M - m_start < TILE_M) ? (M - m_start) : TILE_M;

        for (int n_tile = 0; n_tile < N_tiles; n_tile++) {
            int n_start = n_tile * TILE_N;
            int n_act   = (N - n_start < TILE_N) ? (N - n_start) : TILE_N;

            // 清零部分和
            memset(c_tile, 0, TILE_M * TILE_N * sizeof(q8_8_acc_t));

            for (int k_tile = 0; k_tile < K_tiles; k_tile++) {
                int k_start = k_tile * TILE_K;
                int k_act   = (K - k_start < TILE_K) ? (K - k_start) : TILE_K;

                // 填充 A tile (行主序, 零填充边界)
                for (int i = 0; i < TILE_M; i++) {
                    for (int k = 0; k < TILE_K; k++) {
                        if (i < m_act && k < k_act) {
                            a_tile[i * TILE_K + k] =
                                A_fixed[(m_start + i) * K + (k_start + k)];
                        } else {
                            a_tile[i * TILE_K + k] = 0;
                        }
                    }
                }

                // 填充 B tile (行主序, 零填充边界)
                for (int k = 0; k < TILE_K; k++) {
                    for (int j = 0; j < TILE_N; j++) {
                        if (k < k_act && j < n_act) {
                            b_tile[k * TILE_N + j] =
                                B_fixed[(k_start + k) * N + (n_start + j)];
                        } else {
                            b_tile[k * TILE_N + j] = 0;
                        }
                    }
                }

                // DMA 发送 A tile
                if (send_tile_A(a_tile, m_act, k_act) != XST_SUCCESS)
                    return XST_FAILURE;

                // DMA 发送 B tile
                if (send_tile_B(b_tile, k_act, n_act) != XST_SUCCESS)
                    return XST_FAILURE;

                // DMA 接收 C tile (部分和)
                if (recv_tile_C(c_tile, m_act, n_act) != XST_SUCCESS)
                    return XST_FAILURE;

                // 累加: C_partial += c_tile
                for (int i = 0; i < m_act; i++) {
                    for (int j = 0; j < n_act; j++) {
                        C_fixed[(m_start + i) * N + (n_start + j)] +=
                            c_tile[i * TILE_N + j];
                    }
                }
            } // k_tile
        } // n_tile
    } // m_tile

    // --- 3. 反量化 Q8.8 → float ---
    for (int i = 0; i < M * N; i++) {
        C_float[i] = Q8_8_TO_FLOAT(C_fixed[i]);
    }

    return XST_SUCCESS;
}

// ============================================================================
// 软件参考实现 (用于验证)
// ============================================================================
void matmul_sw(float *A, float *B, float *C, int M, int N, int K) {
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

// ============================================================================
// 测试入口
// ============================================================================
int main(void) {
    xil_printf("========================================\r\n");
    xil_printf(" FPGA 矩阵乘法加速器 — 裸机测试 v1.0\r\n");
    xil_printf("========================================\r\n");

    // 初始化 DMA
    if (dma_init() != XST_SUCCESS) {
        xil_printf("[FATAL] DMA 初始化失败, 停止\r\n");
        return -1;
    }

    // 测试配置
    int M = 32, N = 32, K = 32;
    float A[MAX_M * MAX_K];
    float B[MAX_K * MAX_N];
    float C_hw[MAX_M * MAX_N];
    float C_sw[MAX_M * MAX_N];

    // 填充测试数据
    srand(42);
    for (int i = 0; i < M * K; i++)
        A[i] = ((float)(rand() % 4096) / 256.0f) - 8.0f;
    for (int i = 0; i < K * N; i++)
        B[i] = ((float)(rand() % 4096) / 256.0f) - 8.0f;

    // 软件参考
    matmul_sw(A, B, C_sw, M, N, K);

    // 硬件计算
    XTime tStart, tEnd;
    XTime_GetTime(&tStart);
    int ret = matmul_hw(A, M, K, B, K, N, C_hw);
    XTime_GetTime(&tEnd);

    if (ret != XST_SUCCESS) {
        xil_printf("[FATAL] 硬件计算失败\r\n");
        return -1;
    }

    // 时间统计
    u64 elapsed_us = (tEnd - tStart) * 1000000ULL / COUNTS_PER_SECOND;
    xil_printf("\r\n[INFO] 耗时: %llu us\r\n", elapsed_us);

    // 误差验证
    float max_err = 0.0f;
    for (int i = 0; i < M * N; i++) {
        float err = fabsf(C_hw[i] - C_sw[i]);
        if (err > max_err) max_err = err;
    }
    xil_printf("[INFO] 最大误差: %f\r\n", max_err);
    xil_printf("[INFO] 结果: %s\r\n",
               max_err < 0.02f ? "✓ 通过" : "✗ 失败");

    return 0;
}
