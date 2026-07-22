param(
    [string]$BoardIp = "192.168.2.99",
    [string]$User = "xilinx",
    [string]$RemoteDir = "~/pynq/overlays/matmul_systolic"
)

$ErrorActionPreference = "Stop"
$bit = "vivado/vivado_proj/matmul_system.runs/impl_1/design_1_wrapper.bit"
$hwh = "vivado/vivado_proj/matmul_system.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh"

foreach ($file in @($bit, $hwh)) {
    if (-not (Test-Path $file)) { throw "Missing build artifact: $file" }
}

ssh "${User}@${BoardIp}" "mkdir -p $RemoteDir"
scp $bit "${User}@${BoardIp}:$RemoteDir/design_1.bit"
scp $hwh "${User}@${BoardIp}:$RemoteDir/design_1.hwh"
Write-Host "Deployed matching systolic bit and hwh."
Write-Host "axis_test.ipynb targets the separate design_2 reference hardware and is not copied."
