# AGENTS.md

Read `README.md` first for architecture, design params, and the MAC parallelism
experiment. This file only captures what an agent would otherwise get wrong.

## Toolchain

- **Vitis HLS 2020.2** at `E:\xilinx\Vitis_HLS\2020.2`, **Vivado 2020.2** at
  `E:\xilinx\Vivado\2020.2`. Not on `PATH` by default; env loaded from
  `~/.bashrc` / `~/.bash_profile`.
- Shell is **Git Bash** (paths like `/e/fpga-ai/...`).
- Target part `xc7z020clg400-2` (Zynq-7020, **-2** speed grade). The `-2`
  matters — `-1` was replaced; Fmax 143 MHz only achievable at `-2`.
- No npm/make/CI/lint/typecheck. The only verification is Vitis HLS C-sim
  (`csim_design`) via the TCL scripts below.

## Build flows — two architectures (original + systolic), two paths each

`cd` into the directory containing the TCL script before running.

**Original** (II=24, BRAM accumulator RAW bottleneck):

| Script (run from) | Steps | Clock | Source |
|---|---|---|---|
| `hls/run_diag.tcl` (`hls/`) | csim only — minimal, 1 tile all-1.0 | 10ns | `src/` + `tb/diag_tb.cpp` |
| `hls/run_hls.tcl` (`hls/`) | csim → csynth → export IP | 10ns | `src/` + `tb/matmul_tb.cpp` (5 cases) |
| `hls/build/run.tcl` (`hls/build/`) | csim → csynth → export IP | 10ns | flattened `matmul_all.cpp` |
| `hls/build/run_single.tcl` (`hls/build/`) | csynth → export IP (no csim) | **20ns (50MHz)** | flattened `matmul_all.cpp` |

**Systolic array** (II=1, output-stationary 4×4 PE grid, register accumulators):

| Script (run from) | Steps | Clock | Source |
|---|---|---|---|
| `hls/run_systolic.tcl` (`hls/`) | csim → csynth → export IP | 10ns | `src/matmul_systolic_*.cpp` + `tb/matmul_tb.cpp` |
| `hls/build/run_systolic_single.tcl` (`hls/build/`) | csynth → export IP | **10ns (100MHz)** | flattened `matmul_systolic_all.cpp` |

```bash
cd /e/fpga-ai/hls        && vitis_hls -f run_diag.tcl              # fast functional check
cd /e/fpga-ai/hls        && vitis_hls -f run_hls.tcl               # full C-sim (5 cases)
cd /e/fpga-ai/hls/build  && vitis_hls -f run_single.tcl            # original synthesis

cd /e/fpga-ai/hls        && vitis_hls -f run_systolic.tcl          # systolic csim + csynth
cd /e/fpga-ai/hls/build  && vitis_hls -f run_systolic_single.tcl   # systolic synthesis only
```

The systolic top-level (`matmul_systolic_top.cpp`) exports the same `matmul_top()`
function signature — testbenches and PS drivers are reused unchanged.

### Why the `hls/build/` single-file flow exists

Vitis HLS 2020.2 `clang-tidy` has two hard limits the flattened `matmul_all.cpp`
works around:
1. **Does not parse `#include "..."`** → all code flattened into one file for
   synthesis.
2. **Does not expand C macros in `#pragma` args** → pragmas must use literals.

Consequence: `hls/src/matmul_core.cpp` uses
`#pragma HLS ARRAY_PARTITION ... factor=PARALLEL_N` (macro), while
`hls/build/matmul_all.cpp` rewrites the same pragmas to `factor=4` (literal).
The systolic version has the same dual-copy issue: `hls/src/matmul_systolic_core.cpp`
uses `factor=SYSTOLIC_SIZE`, while `hls/build/matmul_systolic_all.cpp` uses
`factor=4`. When changing partition factors or `PARALLEL_N`/`SYSTOLIC_SIZE`,
update **both** copies or the single-file synthesis silently diverges.

Also: `hls/build/run.tcl` hardcodes an absolute include path
`-IE:/fpga-ai/hls/build` — breaks if the repo is moved.

## Known toolchain bugs

- **Vivado 2020.2 IP packager Y2K22 bug** (FIXED): `export_design -format
  ip_catalog` failed with `bad lexical cast: core_revision` (date stamp →
  int32 overflow after 2022-01-01). Fixed by applying the official Xilinx
  **y2k22 patch**: run `python.exe y2k22_patch/patch.py` from the
  `E:\xilinx` install root (NOT the patch directory — the script uses
  `os.getcwd()` as install root). Patch copies `automg_patch_20220104.tcl`
  into `Vitis_HLS/2020.2/common/scripts/` and `Vivado/2020.2/common/scripts/`.
  After patching, `export_design` works and produces `export.zip`.
  `build_bd_systolic.tcl` still uses RTL-direct (`create_bd_cell -type module
  -reference`) which works with or without the patch, so patching is optional
  for the systolic flow but required if you want IP catalog integration.
- **Block Design** — two scripts, both RTL-direct (neither uses IP catalog):
  - `vivado/build_bd.tcl` — original, expects IP at
    `../hls/proj/matmul_accel/solution1/impl/ip`; **broken** by the IP packager
    bug. Adapt or skip.
  - `vivado/build_bd_systolic.tcl` — **working** systolic flow. Adds HLS RTL
    via `add_files` + `create_bd_cell -type module -reference matmul_top`,
    bypassing the IP packager entirely. 3 unidirectional AXI DMA (A/B MM2S,
    C S2MM, 16-bit axis) → SmartConnect → Zynq HP0. PS7 configured manually
    (no board preset; DDR3 `MT41J256M16 RE-125` — note **J**, matching
    领航者 board's SK Hynix H5TC4G63EFR-RDA). Runs synth+impl+write_bitstream
    and also generates a `.bin` (byte-swapped, for FPGA manager).
    Produces bitstream at
    `vivado_proj/matmul_system.runs/impl_1/design_1_wrapper.bit`,
    `.bin` at `vivado/design_1_wrapper.bin`, and hardware handoff at
    `.../matmul_system.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh`.
    Verified: WNS=+1.24ns @100MHz, 0 failing endpoints, ~11 min wall time.

  Block Design gotchas baked into `build_bd_systolic.tcl`:
  - HLS RTL AXIS interfaces are named with a `_V` suffix (`in_A_V`, `in_B_V`,
    `out_C_V`) — connect to `matmul_top_0/in_A_V`, not `/in_A`.
  - DMA S2MM axis-width param is `c_s_axis_s2mm_tdata_width` (not `c_m_axis_`).
  - Every AXI Interconnect master/slave port needs its own `*_ACLK` pin wired
    to FCLK_CLK0, plus each DMA's `s_axi_lite_aclk` — `validate_bd_design`
    fails on any dangling clock pin.
  - **FCLK clock chain must be fully configured** (not just target freq):
    `PCW_FPGA0_PERIPHERAL_FREQMHZ=100` alone is NOT enough. Must also set
    `PCW_FCLK0_PERIPHERAL_CLKSRC=IO PLL`, `DIVISOR0=5`, `DIVISOR1=2`,
    `PCW_FCLK_CLK0_BUF=TRUE`. Missing any → FCLK_CLK0 outputs nothing →
    PL side has no clock → first AXI-Lite read hangs the CPU → board dies.
    This was the root cause of 3 board crashes before diagnosis.
  - **SLCR write protection**: `FCLK_CTRL0` (0xF8000170) is write-protected.
    Must write unlock key `0xDF0D` to `SLCR_UNLOCK` (0xF8000008) before
    writing, then `0x767B` to `SLCR_LOCK` (0xF8000004). Without unlock,
    the CLKACT bit silently fails to stick. `APER_CLK_CTRL` (0xF8000138)
    is NOT protected. Enable FCLK **before** AXI clocks, or the bus
    crashes (SSH dies, serial survives). See `pynq_driver._enable_fclk()`.
  - **FCLK must be enabled via Pynq Clocks API, NOT direct SLCR write**.
    On Pynq v2.7, the Linux `clk-fclk-zynq` driver claims FCLK registers —
    direct `/dev/mem` or MMIO writes to `FCLK_CTRL0` are silently ignored
    (even with SLCR unlocked, `SLCR_PROT=0`). Use `pynq.Clocks.fclk0 = 100`
    which goes through the kernel clock framework. `APER_CLK_CTRL` is not
    claimed and can be written directly.
  - **`ASSOCIATED_BUSIF` on `matmul_top_0/ap_clk` is mandatory** — without
    it, Vivado reports `[BD 41-967] AXI interface not associated to any
    clock pin`, the .hwh doesn't record the clock dependency, and Pynq
    `Overlay.download()` doesn't enable FCLK0 → PL has no clock → AXI
    hangs → board dies. Fix:
    `set_property CONFIG.ASSOCIATED_BUSIF {s_axi_control:in_A_V:in_B_V:out_C_V} [get_bd_pins matmul_top_0/ap_clk]`
    (PS7's FCLK_CLK0 pin is read-only, can't set ASSOCIATED_BUSIF there.)
  - **PS7 parameters must be set in a single `set_property -dict` call**.
    Splitting DDR3 + MIO + FCLK into separate calls causes parameter
    dependency resets (later calls silently clear earlier ones).
  - **DDR3 PARTNO matters**: 领航者板 uses `MT41J256M16 RE-125` (note **J**,
    not K — different chip, different row address count). Also
    `T_RCD`/`T_RP` units are **cycles** (value=7 for DDR3_1066F), not ns.
  - **MIO/peripheral config differs from ZC702 default**: UART0=MIO 14-15,
    SD0=MIO 40-45 (CD on 10), Enet0=MIO 16-27, USB0=MIO 28-39 (reset on 9),
    QSPI=MIO 1-6, SD1=MIO 46-51. Wrong MIO → no serial/net/SD after boot.

  Board-side diagnostic: `ps_driver/diag_pl.py` — 4-step PL health check
  (bitstream download → FCLK_CLK0 verify via SLCR → DMA regs → HLS IP regs).
  Run BEFORE the full overlay/notebook to localize a hang.

## Tech debt — fix before any production build

These live in code but contradict the README baseline; synthesizing without
checking produces the wrong config:

- `PARALLEL_N` is **4** in every source (`hls/src/matmul.h`, `hls/build/matmul.h`,
  `hls/build/matmul_all.cpp`). Experiment residue. Documented production baseline
  is **N=2 @100MHz**. Revert to 2 and change `factor=4` → `factor=2` literals in
  `matmul_all.cpp`.
- `hls/build/run_single.tcl` uses `create_clock -period 20.0` (50MHz). Production
  N=2 needs **10ns (100MHz)**.

## Gitignored build outputs — not source, do not edit

Generated by the TCL scripts, `.gitignore`d:
- `matmul_single/` (repo root) — output of `hls/build/run_single.tcl`
- `hls/matmul_accel/`, `hls/proj/`, `hls/diag_test/` — from `run_hls.tcl` / `run_diag.tcl`
- `hls/build/matmul_accel/`, `hls/build/matmul_single/` — from the `build/` scripts
- `hls/systolic_accel/`, `hls/build/systolic_single/` — from the systolic scripts
- any `**/solution*/` — contains generated RTL under `syn/verilog/`, `syn/vhdl/`

Source of truth: `hls/src/` (multi-file) and `hls/build/matmul_all.cpp`
(flattened synthesis copy). Systolic equivalents: `hls/src/matmul_systolic_*.cpp`
and `hls/build/matmul_systolic_all.cpp`.

## PS-side drivers (`ps_driver/`)

Run **on the Zynq board**, not the dev machine:
- `pynq_driver.py` — Pynq v2.7 driver for the systolic BD. Targets the
  **3-DMA topology** of `build_bd_systolic.tcl` (`axi_dma_A`/`B` MM2S +
  `axi_dma_C` S2MM), NOT a single bidirectional DMA. HLS IP register offsets:
  `M=0x10, N=0x18, K=0x20` (NOT 0x08/0x0C/0x10 — the old `baremetal_driver.c`
  has the wrong offsets; the RTL `matmul_top_control_s_axi.v` is the source of
  truth). Stream protocol: send all A/B `k_tile`s, then recv one C_tile per
  `(m,n)` tile (the IP accumulates K internally). `benchmark()` method returns
  `{size: {latency_ms, gops, max_err, ...}}` for the Jupyter plots.
- `baremetal_driver.c` — imported into a Vitis (formerly SDK) project on board.
  **Stale**: wrong DMA topology (single), wrong register offsets, wrong stream
  protocol. Use `pynq_driver.py` or rewrite before use.
- `notebooks/bench_matmul.ipynb` — Jupyter notebook with 4 plots (latency bar,
  throughput curve, 64×64 heatmap, error histogram). Run on the board's
  Jupyter (port 9090).
- `notebooks/diag_pl.ipynb` — 4-step PL health check (overlay load → FCLK
  verify via Pynq Clocks → DMA regs → HLS IP regs). Run BEFORE
  `bench_matmul.ipynb` to localize a hang.
- `diag_pl.py` — command-line version of the PL diagnostic (needs `sudo`).
- `deploy.ps1` — PowerShell script that copies bitstream + .hwh + .bin +
  driver + notebooks to the board. `.\deploy.ps1 -IncludeNotebook` also
  copies `bench_matmul.ipynb`.

### Board deployment (Pynq v2.7)

1. Build bitstream: `vivado -mode batch -source build_bd_systolic.tcl` (from
   `vivado/`). Produces `design_1_wrapper.bit` + `.bin` + `design_1.hwh`.
2. Deploy: `.\deploy.ps1` (from repo root, in PowerShell).
3. Open `http://<board_ip>:9090` → run `diag_pl.ipynb` first, then
   `bench_matmul.ipynb`.

## Constraints when editing HLS code

- Data type `ap_fixed<16,8,AP_RND,AP_SAT>` (Q8.8); accumulator
  `ap_fixed<48,32,...>`. Don't narrow without rechecking overflow for K up to 64.
- Tile size fixed 16×16×16; PS driver always sends full `TILE×TILE` elements,
  zero-padding boundary tiles.
- Inner `k` loop has a true RAW dependency → HLS infers **II=24**, the real
  throughput bottleneck. Adding MACs does not help (verified in README); the
  fix is the Systolic Array (`hls/src/matmul_systolic_core.cpp`, II=1), not more
  parallel MACs.
- The systolic core uses a different B_buf partition dimension: **dim=1 (K)**
  vs the original's dim=2 (N). This is required so the array's top edge can
  read S different k values per cycle.
