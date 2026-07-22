# Zynq-7020 systolic matrix multiplier

This branch contains one hardware architecture and one board integration path.
It intentionally removes the original parallel-MAC experiment and stale board
drivers.

## Architecture

- Device: `xc7z020clg400-2`
- Arithmetic: signed Q8.8 input/output, 48-bit accumulator
- Tile: 16x16x16
- Compute: output-stationary 4x4 systolic array
- Control: AXI4-Lite (`M=0x10`, `N=0x18`, `K=0x20`)
- Data: three 32-bit AXI4-Stream interfaces carrying sign-extended Q8.8 values

Every A and B DMA transfer is one 16x16 tile. Every C transfer is one 16x16
tile and the accelerator asserts `TLAST` on element 255. The explicit packet
boundary is required by AXI DMA S2MM.

## Source layout

```text
hls/src/                       readable source of truth
  matmul_systolic_core.cpp     preserved 4x4 PE implementation
  matmul_systolic_top.cpp      tiling and AXI interfaces
  matmul*.h                    types and declarations
hls/tb/matmul_tb.cpp           arithmetic and TLAST C simulation
hls/build/
  matmul_systolic_all.cpp      flattened Vitis HLS 2020.2 synthesis copy
  run_systolic_single.tcl
vivado/build_bd_systolic.tcl   PS7 + three DMA + RTL-direct integration
ps_driver/notebooks/
  axis_test.ipynb              notebook derived from verified test.py
deploy.ps1                     board deployment
```

The flattened HLS file is deliberately retained for Vitis HLS 2020.2. When
changing the readable source, update the flattened copy in the same commit.

## Build

Run from Git Bash after loading Vitis/Vivado 2020.2:

```bash
cd /e/fpga-ai/hls
vitis_hls -f run_systolic.tcl

cd /e/fpga-ai/hls/build
vitis_hls -f run_systolic_single.tcl

cd /e/fpga-ai/vivado
vivado -mode batch -source build_bd_systolic.tcl
```

Before deployment, verify the generated `matmul_top.v` contains
`out_C_TLAST` (the exact prefix may include `_V`). Then deploy from PowerShell:

```powershell
.\deploy.ps1
```

## DDR configuration

The PS7 DDR part is `MT41K256M16 RE-125`. Selecting a 512 MiB-compatible part
while booting a Pynq image configured for the board's real 1 GiB memory can
place allocated DMA buffers outside the address range implemented by the PS7
configuration. The resulting memory access failure can look like an AXIS
deadlock even when the stream wiring is correct.

## Board notebook

`ps_driver/notebooks/axis_test.ipynb` targets the current `design_1.bit`:
`axi_dma_A` and `axi_dma_B` provide MM2S inputs, `axi_dma_C` receives S2MM
results, and `matmul_top_0` supplies AXI-Lite control. AXIS words are int32
containers for sign-extended Q8.8 values. The notebook starts with one 16x16
tile and uses bounded polling with DMA/HLS status output instead of unbounded
`wait()` calls. It still requires validation with a freshly built bitstream.
