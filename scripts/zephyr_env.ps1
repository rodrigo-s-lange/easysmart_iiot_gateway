param(
    [string]$ZephyrBase = "C:\zephyrproject\zephyr",
    [string]$ZephyrSdkInstallDir = "C:\zephyr-sdk-0.17.3",
    [int]$BuildParallel = 1
)

$env:ZEPHYR_BASE = $ZephyrBase
$env:ZEPHYR_SDK_INSTALL_DIR = $ZephyrSdkInstallDir
$env:CMAKE_BUILD_PARALLEL_LEVEL = "$BuildParallel"

$venvActivate = "C:\zephyrproject\.venv\Scripts\Activate.ps1"
if (Test-Path $venvActivate) {
    . $venvActivate
}
