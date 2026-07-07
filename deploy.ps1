# ============================================================================
# deploy.ps1 — 一键部署 bitstream + 驱动 + 诊断脚本到领航者板
#
# 用法 (在开发机 PowerShell 里):
#   .\deploy.ps1                    # 默认 192.168.2.99
#   .\deploy.ps1 -BoardIp 192.168.2.50
#   .\deploy.ps1 -IncludeNotebook   # 同时传 Jupyter notebook
# ============================================================================
param(
    [string]$BoardIp = "192.168.2.99",
    [string]$User = "xilinx",
    [string]$RemoteDir = "~/pynq/overlays/matmul_systolic",
    [switch]$IncludeNotebook = $false
)

$ErrorActionPreference = "Stop"

# --- 路径 (相对于仓库根) ---
$RepoRoot = "."
$BitFile  = "$RepoRoot\vivado\vivado_proj\matmul_system.runs\impl_1\design_1_wrapper.bit"
$BinFile  = "$RepoRoot\vivado\design_1_wrapper.bin"
$HwhFile  = "$RepoRoot\vivado\vivado_proj\matmul_system.gen\sources_1\bd\design_1\hw_handoff\design_1.hwh"
$DriverFile = "$RepoRoot\ps_driver\pynq_driver.py"
$DiagFile = "$RepoRoot\ps_driver\diag_pl.py"
$NotebookFile = "$RepoRoot\ps_driver\notebooks\bench_matmul.ipynb"
$DiagNotebookFile = "$RepoRoot\ps_driver\notebooks\diag_pl.ipynb"

# --- 检查文件存在 ---
$required = @($BitFile, $BinFile, $HwhFile, $DriverFile, $DiagFile, $DiagNotebookFile)
if ($IncludeNotebook) { $required += $NotebookFile }
foreach ($f in $required) {
    if (-not (Test-Path $f)) {
        Write-Host "[ERROR] 文件不存在: $f" -ForegroundColor Red
        Write-Host "  先运行: vivado -mode batch -source build_bd_systolic.tcl"
        exit 1
    }
}

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host " 部署到领航者板: $User@$BoardIp" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# --- 创建远程目录 ---
Write-Host "[1/5] 创建远程目录 $RemoteDir ..." -ForegroundColor Yellow
ssh "${User}@${BoardIp}" "mkdir -p $RemoteDir ~/jupyter_notebooks"
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] SSH 连接失败, 检查板子是否在线: ping $BoardIp" -ForegroundColor Red
    exit 1
}

# --- 传 bitstream (重命名为 design_1.bit) ---
Write-Host "[2/6] 传 bitstream (3.9 MB) ..." -ForegroundColor Yellow
scp $BitFile "${User}@${BoardIp}:$RemoteDir/design_1.bit"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] scp bitstream 失败" -ForegroundColor Red; exit 1 }

# --- 传 .bin (FPGA manager 直接加载, 不需运行时转换) ---
Write-Host "[3/6] 传 .bin 文件 (FPGA manager 用) ..." -ForegroundColor Yellow
scp $BinFile "${User}@${BoardIp}:$RemoteDir/design_1.bin"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] scp bin 失败" -ForegroundColor Red; exit 1 }

# --- 传 hwh ---
Write-Host "[4/6] 传 hardware handoff (.hwh) ..." -ForegroundColor Yellow
scp $HwhFile "${User}@${BoardIp}:$RemoteDir/design_1.hwh"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] scp hwh 失败" -ForegroundColor Red; exit 1 }

# --- 传驱动 + 诊断脚本 ---
Write-Host "[5/6] 传 pynq_driver.py + diag_pl.py + diag_pl.ipynb ..." -ForegroundColor Yellow
scp $DriverFile $DiagFile "${User}@${BoardIp}:$RemoteDir/"
scp $DiagNotebookFile "${User}@${BoardIp}:~/jupyter_notebooks/diag_pl.ipynb"
if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] scp 驱动失败" -ForegroundColor Red; exit 1 }

# --- 传 notebook (可选) ---
if ($IncludeNotebook) {
    Write-Host "[6/6] 传 bench_matmul.ipynb ..." -ForegroundColor Yellow
    scp $NotebookFile "${User}@${BoardIp}:~/jupyter_notebooks/bench_matmul.ipynb"
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] scp notebook 失败" -ForegroundColor Red; exit 1 }
} else {
    Write-Host "[6/6] 跳过 bench notebook (加 -IncludeNotebook 参数来传)" -ForegroundColor DarkGray
}

Write-Host "`n========================================" -ForegroundColor Green
Write-Host " 部署完成!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host "`n下一步:" -ForegroundColor Cyan
Write-Host "  1. SSH 上板: ssh $User@$BoardIp"
Write-Host "  3. 诊断:   http://${BoardIp}:9090 → 打开 diag_pl.ipynb"
Write-Host ""
