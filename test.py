from pynq import Overlay, allocate
import numpy as np

overlay = Overlay(
    "design_2.bit"
)
MAT_A_ROWS = 32
MAT_A_COLS = 32
MAT_B_ROWS = 32
MAT_B_COLS = 32

dma_0 = overlay.axi_dma_0
dma_1 = overlay.axi_dma_1

A = np.random.randint(0, 127, (MAT_A_ROWS, MAT_A_COLS), dtype=np.uint32)
B = np.random.randint(0, 127, (MAT_B_ROWS, MAT_B_COLS), dtype=np.uint32)
golden = np.matmul(A.astype(np.uint64), B.astype(np.uint64)).astype(np.uint32)

a_buf = allocate(shape=(MAT_A_ROWS * MAT_A_COLS,), dtype=np.uint32)
b_buf = allocate(shape=(MAT_B_ROWS * MAT_B_COLS,), dtype=np.uint32)
res_buf = allocate(shape=(MAT_A_ROWS * MAT_B_COLS,), dtype=np.uint32)
np.copyto(a_buf, A.flatten())
np.copyto(b_buf, B.flatten())

dma_0.sendchannel.transfer(b_buf)
dma_1.sendchannel.transfer(a_buf)
dma_0.sendchannel.wait()
dma_1.sendchannel.wait()
dma_0.recvchannel.transfer(res_buf)

dma_0.recvchannel.wait()

result = res_buf.reshape(MAT_A_ROWS, MAT_B_COLS).astype(np.int32)
print("Golden result:")
print(golden)
print("Hardware result:")
print(result)