
#include "example_hls.h"
#include <ap_axi_sdata.h>
#include <hls_stream.h>

// 假设定义
typedef ap_axis<32, 0, 0, 0> axis_t;

void matrixmul(
    hls::stream<axis_t> &a,
    hls::stream<axis_t> &b,
    hls::stream<axis_t> &res)
{
    // 保持你的 directive 配置
    #pragma HLS INTERFACE ap_ctrl_none port=return
    #pragma HLS INTERFACE axis register both port=a
    #pragma HLS INTERFACE axis register both port=b
    #pragma HLS INTERFACE axis register both port=res

    int tempA[MAT_A_ROWS][MAT_A_COLS];
    int tempB[MAT_B_ROWS][MAT_B_COLS];
    int tempAB[MAT_A_ROWS][MAT_B_COLS];

    // 从输入流 a 中读取数据
    for (int ia = 0; ia < MAT_A_ROWS; ia++) {
        for (int ja = 0; ja < MAT_A_COLS; ja++) {
            #pragma HLS PIPELINE II=1
            axis_t packet = a.read();
            tempA[ia][ja] = packet.data; // 获取数据
            // packet.last 可以用来校验输入边界，这里暂不需要
        }
    }

    // 从输入流 b 中读取数据
    for (int ib = 0; ib < MAT_B_ROWS; ib++) {
        for (int jb = 0; jb < MAT_B_COLS; jb++) {
            #pragma HLS PIPELINE II=1
            axis_t packet = b.read();
            tempB[ib][jb] = packet.data;
        }
    }

    /* 矩阵相乘核心算法 */
    row: for(int i = 0; i < MAT_A_ROWS; ++i) {
        col: for(int j = 0; j < MAT_B_COLS; ++j) {
            int ABij = 0;
            product: for(int k = 0; k < MAT_A_COLS; ++k) {
                #pragma HLS PIPELINE II=1
                ABij += tempA[i][k] * tempB[k][j];
            }
            tempAB[i][j] = ABij;
        }
    }

    // 将结果写入输出流 res，并生成 TLAST
    for (int iab = 0; iab < MAT_A_ROWS; iab++) {
        for (int jab = 0; jab < MAT_B_COLS; jab++) {
            #pragma HLS PIPELINE II=1

            axis_t packet;
            packet.data = tempAB[iab][jab];
            packet.keep = -1; // 保持所有字节有效
            packet.strb = -1;

            // 关键：当发送矩阵的最后一个元素时，拉高 TLAST
            if (iab == (MAT_A_ROWS - 1) && jab == (MAT_B_COLS - 1)) {
                packet.last = 1;
            } else {
                packet.last = 0;
            }

            res.write(packet); // 发送数据包
        }
    }
}
