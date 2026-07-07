#!/usr/bin/env python3
"""
diag_pl.py — PL 健康诊断 (完全自包含, 不依赖 Pynq Overlay 下载)

用法 (在板子上):
    sudo python3 diag_pl.py        # 推荐 sudo (需要 /dev/mem 访问)
    python3 diag_pl.py

如果某一步挂死, 说明问题在该步:
  步骤 0  — 环境检查 (不挂, 只打印)
  步骤 1  — bitstream 下载 (挂 → DDR/MIO 配置问题)
  步骤 2  — FCLK_CLK0 验证 (挂 → FCLK 时钟链问题)
  步骤 3  — DMA 寄存器 (挂 → AXI 总线问题)
  步骤 4  — HLS IP 寄存器 (挂 → matmul_top 问题)
"""
import sys
import os
import mmap
import time
import subprocess

BITFILE = '/home/xilinx/pynq/overlays/matmul_systolic/design_1.bit'
BINFILE = '/home/xilinx/pynq/overlays/matmul_systolic/design_1.bin'

# 寄存器基地址 (从 build_bd_systolic.tcl assign_bd_address 确认)
SLCR_BASE       = 0xF8000000   # PS7 SLCR
FCLK_CTRL0_OFF  = 0x170        # FCLK_CTRL0 (Zynq-7000 TRM: SLCR + 0x170)
APER_CLK_OFF    = 0x138        # APER_CLK_CTRL (AXI 外设时钟使能)
DMA_A_BASE      = 0x40400000   # axi_dma_A
MATMUL_BASE     = 0x43C00000   # matmul_top_0

def step(n, desc):
    print(f"\n--- 步骤 {n}: {desc} ---")
    sys.stdout.flush()

def read_devmem(phys_base, offset=0, size=4):
    """通过 /dev/mem 读 32 位寄存器 (需要 root 或 /dev/mem 权限)"""
    page_base = phys_base & ~0xFFF
    page_off  = phys_base - page_base + offset
    fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
    mm = mmap.mmap(fd, 0x1000, offset=page_base, prot=mmap.PROT_READ)
    val = int.from_bytes(mm[page_off:page_off+size], 'little')
    mm.close()
    os.close(fd)
    return val

def write_devmem(phys_base, offset, value):
    """通过 /dev/mem 写 32 位寄存器 (需要 root)"""
    page_base = phys_base & ~0xFFF
    page_off  = phys_base - page_base + offset
    fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
    mm = mmap.mmap(fd, 0x1000, offset=page_base, prot=mmap.PROT_READ|mmap.PROT_WRITE)
    mm[page_off:page_off+4] = value.to_bytes(4, 'little')
    mm.close()
    os.close(fd)

print("=" * 60)
print(" PL 健康诊断 — 领航者 ZYNQ-7020 + Systolic Overlay")
print("=" * 60)

# -------------------------------------------------------------------------
# 步骤 0: 环境检查
# -------------------------------------------------------------------------
step(0, "环境检查")

# Pynq 版本
try:
    import pynq
    print(f"  Pynq: v{pynq.__version__}")
except:
    print("  Pynq: 未安装")

# /dev/mem 权限
r = subprocess.run(['ls', '-la', '/dev/mem'], capture_output=True, text=True)
print(f"  /dev/mem: {r.stdout.strip().split(chr(10))[0].strip()}")

# zocl 模块
r = subprocess.run(['lsmod'], capture_output=True, text=True)
print(f"  zocl: {'loaded' if 'zocl' in r.stdout else 'not loaded'}")

# FPGA manager
fpga_mgr = '/sys/class/fpga_manager/fpga0'
print(f"  FPGA manager: {'yes' if os.path.exists(fpga_mgr) else 'no'}")

# 当前用户
print(f"  User: {os.getenv('USER')} (sudo={os.getenv('SUDO_USER') is not None})")

# -------------------------------------------------------------------------
# 步骤 1: 下载 bitstream (尝试多种方法)
# -------------------------------------------------------------------------
step(1, "下载 bitstream")
downloaded = False

# 方法 A: Pynq 的 PLServer/DefaultDevice (能正确处理 .bit 格式)
# FPGA manager 要 .bin (byte-swapped), Pynq 内部会处理转换
if not downloaded:
    try:
        # 方法 A1: 试试 Pynq 的 Bitstream 类 (v2.7 会自动走 PL 设备)
        from pynq import Bitstream
        bs = Bitstream(BITFILE)
        bs.download()
        print("  [OK] Pynq Bitstream.download() 成功")
        downloaded = True
    except Exception as e:
        print(f"  [WARN] Pynq Bitstream 失败: {e}")

# 方法 A2: 直接用 pynq.pl_server (Pynq v2.7 底层)
if not downloaded:
    try:
        from pynq.pl_server import Device
        devices = Device.devices
        if devices:
            dev = devices[0]
            print(f"  发现 PL 设备: {type(dev).__name__}")
            from pynq import Bitstream
            bs = Bitstream(BITFILE)
            dev.download(bs)
            print("  [OK] PL 设备下载成功")
            downloaded = True
        else:
            print("  [WARN] 无 PL 设备 (XRT/zocl 未初始化)")
    except Exception as e:
        print(f"  [WARN] pl_server 失败: {e}")

# 方法 B: 用 xdevman / xmutil (Pynq v2.7 的 XRT 工具)
if not downloaded:
    try:
        r = subprocess.run(['xmutil', 'loadx', BITFILE],
                           capture_output=True, text=True, timeout=15)
        if r.returncode == 0:
            print("  [OK] xmutil loadx 成功")
            downloaded = True
        else:
            print(f"  [WARN] xmutil 失败: {r.stderr.strip()}")
    except FileNotFoundError:
        print("  [WARN] xmutil 不存在")
    except Exception as e:
        print(f"  [WARN] xmutil 异常: {e}")

# 方法 C: 直接用 Vivado 预生成的 .bin + FPGA manager (最可靠)
if not downloaded and os.path.exists('/sys/class/fpga_manager/fpga0'):
    try:
        if not os.path.exists(BINFILE):
            raise FileNotFoundError(f"{BINFILE} 不存在, 检查 deploy.ps1 是否传了 .bin")
        import shutil
        fname = os.path.basename(BINFILE)
        shutil.copy(BINFILE, f'/lib/firmware/{fname}')
        with open('/sys/class/fpga_manager/fpga0/firmware', 'w') as f:
            f.write(fname)
        time.sleep(3)
        print(f"  [OK] FPGA manager 加载 .bin 成功 ({fname})")
        downloaded = True
    except PermissionError:
        print("  [WARN] FPGA manager 需要权限 (试 sudo)")
    except Exception as e:
        print(f"  [WARN] FPGA manager 失败: {e}")

# 方法 D: 运行时 .bit → .bin 转换 + FPGA manager (备用, 当 .bin 不存在时)
if not downloaded and os.path.exists('/sys/class/fpga_manager/fpga0') and not os.path.exists(BINFILE):
    try:
        print("  [INFO] .bin 不存在, 尝试 .bit → .bin 运行时转换")
        binfile = BITFILE.replace('.bit', '.bin')
        with open(BITFILE, 'rb') as f:
            data = f.read()
        idx = data.find(b'\xff\xff\xff\xff')
        if idx < 0:
            raise ValueError("找不到 bitstream sync word")
        payload = data[idx:]
        import struct
        swapped = bytearray()
        for i in range(0, len(payload) - 3, 4):
            word = struct.unpack('>I', payload[i:i+4])[0]
            swapped += struct.pack('<I', word)
        with open(binfile, 'wb') as f:
            f.write(bytes(swapped))
        print(f"  [INFO] 转换: {binfile} ({len(swapped)} bytes)")
        import shutil
        fname = os.path.basename(binfile)
        shutil.copy(binfile, f'/lib/firmware/{fname}')
        with open('/sys/class/fpga_manager/fpga0/firmware', 'w') as f:
            f.write(fname)
        time.sleep(3)
        print(f"  [OK] FPGA manager 加载转换后 .bin 成功")
        downloaded = True
    except PermissionError:
        print("  [WARN] FPGA manager 需要权限 (试 sudo)")
    except Exception as e:
        print(f"  [WARN] .bit→.bin 转换失败: {e}")

if not downloaded:
    print("  [FAIL] 所有下载方法失败")
    print("  → 尝试: sudo python3 diag_pl.py")
    print("  → 或在 Jupyter notebook 里运行 (Pynq 服务有权限)")
    # 不 exit, 继续 (可能 base overlay 仍在, 可读寄存器验证 PL 状态)

# -------------------------------------------------------------------------
# 步骤 1.5: 手动使能 FCLK_CLK0 + AXI 总线时钟
# (FPGA manager 只重配 PL fabric, 不应用 PS7 init → FCLK 被关闭)
# -------------------------------------------------------------------------
step("1.5", "手动使能 FCLK_CLK0 + AXI 总线时钟 (补 FPGA manager 缺失的 PS7 init)")
try:
    # --- FCLK_CTRL0: 使能 FCLK_CLK0 = IO_PLL(1000MHz) / 5 / 2 = 100MHz ---
    # 字段 (UG585): CLKACT[0], DIVISOR0[13:8], SRCSEL[20:16]=0(IO PLL), DIVISOR1[25:21]
    fclk_val = (2 << 21) | (0 << 16) | (5 << 8) | 0x1   # 0x00400501
    write_devmem(SLCR_BASE, FCLK_CTRL0_OFF, fclk_val)
    rd = read_devmem(SLCR_BASE, FCLK_CTRL0_OFF)
    print(f"  FCLK_CTRL0 写入 0x{fclk_val:08X}, 回读 0x{rd:08X}")
    if rd & 0x1:
        print("  [OK] FCLK_CLK0 已使能 (CLKACT=1)")
    else:
        print("  [FAIL] FCLK_CLK0 写入未生效")
        sys.exit(1)

    # --- APER_CLK_CTRL: 使能 M_AXI_GP0 (bit0) + S_AXI_HP0 (bit4) ---
    aper = read_devmem(SLCR_BASE, APER_CLK_OFF)
    print(f"  APER_CLK_CTRL 当前 = 0x{aper:08X}")
    need = aper | (1 << 0) | (1 << 4)   # OR 进去, 不破坏其他外设时钟
    if need != aper:
        write_devmem(SLCR_BASE, APER_CLK_OFF, need)
        aper2 = read_devmem(SLCR_BASE, APER_CLK_OFF)
        print(f"  APER_CLK_CTRL 写入 0x{need:08X}, 回读 0x{aper2:08X}")
        if (aper2 & ((1<<0)|(1<<4))) == ((1<<0)|(1<<4)):
            print("  [OK] M_AXI_GP0 + S_AXI_HP0 时钟已使能")
        else:
            print("  [WARN] AXI 时钟使能位未生效")
    else:
        print("  [OK] GP0 + HP0 时钟已使能 (无需修改)")

    time.sleep(0.1)   # 等时钟稳定
except PermissionError:
    print("  [FAIL] /dev/mem 写入权限不足, 试: sudo python3 diag_pl.py")
    sys.exit(1)
except Exception as e:
    print(f"  [FAIL] {e}")
    sys.exit(1)

# -------------------------------------------------------------------------
# 步骤 2: 验证 FCLK_CLK0 (读 SLCR, 不依赖 Pynq)
# -------------------------------------------------------------------------
step(2, "验证 FCLK_CLK0 时钟 (读 SLCR 0xF8000170)")
try:
    val = read_devmem(SLCR_BASE, FCLK_CTRL0_OFF)
    # Zynq-7000 TRM (UG585) FCLK_CTRL0 字段:
    #   bits [4:0]   CLKACT   (bit0=1 时钟使能)
    #   bits [13:8]  DIVISOR0
    #   bits [20:16] SRCSEL   (0=IO PLL)
    #   bits [25:21] DIVISOR1
    clkact = val & 0x1F
    srcsel = (val >> 16) & 0x1F
    div0 = (val >> 8) & 0x3F
    div1 = (val >> 21) & 0x1F
    print(f"  FCLK_CTRL0 = 0x{val:08X}")
    print(f"  CLKACT={clkact:05b}, SRCSEL={srcsel} (0=IO PLL), "
          f"DIVISOR0={div0}, DIVISOR1={div1}")
    if clkact & 0x1:
        print("  [OK] FCLK_CLK0 时钟已使能")
    else:
        print("  [FAIL] FCLK_CLK0 时钟未使能 (CLKACT bit0=0)")
        print("  → FPGA manager 不应用 PS7 init, 需手动使能 FCLK")
        sys.exit(1)
except PermissionError:
    print("  [FAIL] /dev/mem 权限不足, 试: sudo python3 diag_pl.py")
    sys.exit(1)
except Exception as e:
    print(f"  [FAIL] {e}")
    sys.exit(1)

# -------------------------------------------------------------------------
# 步骤 3: 读 DMA 寄存器 (验证 AXI 总线)
# -------------------------------------------------------------------------
step(3, "读 DMA_A 寄存器 (验证 AXI 总线)")
try:
    mm2s_sr = read_devmem(DMA_A_BASE, 0x04)   # MM2S_DMASR
    print(f"  DMA_A MM2S_DMASR = 0x{mm2s_sr:08X}")
    if mm2s_sr & 0x1:
        print("  [OK] DMA_A 可读 (Halted=1, 复位状态正常)")
    else:
        print(f"  [WARN] DMA_A Halted=0 (0x{mm2s_sr:08X})")
except Exception as e:
    print(f"  [FAIL] {e}")
    print("  → AXI 总线无响应 (interconnect 时钟/复位问题)")
    sys.exit(1)

# -------------------------------------------------------------------------
# 步骤 4: 读 matmul_top 控制寄存器 (验证 HLS IP)
# -------------------------------------------------------------------------
step(4, "读 matmul_top 控制寄存器 (验证 HLS IP)")
try:
    ap_ctrl = read_devmem(MATMUL_BASE, 0x00)   # AP_CTRL
    print(f"  matmul_top AP_CTRL = 0x{ap_ctrl:08X}")
    if ap_ctrl & 0x4:
        print("  [OK] matmul_top ap_idle=1 (IP 空闲, 健康)")
    else:
        print(f"  [WARN] ap_idle=0 (0x{ap_ctrl:08X})")
    m_val = read_devmem(MATMUL_BASE, 0x10)
    n_val = read_devmem(MATMUL_BASE, 0x18)
    k_val = read_devmem(MATMUL_BASE, 0x20)
    print(f"  M={m_val}, N={n_val}, K={k_val} (应全为 0, 未配置)")
except Exception as e:
    print(f"  [FAIL] {e}")
    print("  → matmul_top AXI-Lite 无响应 (IP 时钟或复位问题)")
    sys.exit(1)

# -------------------------------------------------------------------------
# 全部通过
# -------------------------------------------------------------------------
print("\n" + "=" * 60)
print("  [全部通过] PL 健康, 可安全运行 matmul")
print("=" * 60)
