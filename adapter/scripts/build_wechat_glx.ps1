[CmdletBinding()]
param(
    [string]$EmsdkPath = "",
    [ValidateRange(1, 2147483647)]
    [int]$Revision = 1,
    [switch]$Incremental
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ToolRoot = Join-Path $RepoRoot ".build-tools"
$VenvRoot = Join-Path $ToolRoot "scons-4.10.1"
$VenvPython = Join-Path $VenvRoot "Scripts\python.exe"
$SCons = Join-Path $VenvRoot "Scripts\scons.exe"
$SConsWheel = Join-Path $ToolRoot "scons-4.10.1-py3-none-any.whl"
$SConsWheelUrl = "https://files.pythonhosted.org/packages/ce/bf/931fb9fbb87234c32b8b1b1c15fba23472a10777c12043336675633809a7/scons-4.10.1-py3-none-any.whl"
$SConsWheelSha256 = "BD9D1C52F908D874EBA92A8C0C0A8DCF2ED9F3B88AB956D0FCE1DA479C4E7126"
$BrotliRoot = Join-Path $ToolRoot "brotli-1.2.0"
$Brotli = Join-Path $BrotliRoot "brotli.exe"
$Requirements = Join-Path $RepoRoot "adapter\ci\requirements-build.txt"
$BuildScript = Join-Path $RepoRoot "adapter\scripts\package_wechat_glx_template.py"

if (-not $EmsdkPath) {
    $EmsdkPath = $env:EMSDK
}
if (-not $EmsdkPath -and (Test-Path "D:\Tools\emsdk")) {
    $EmsdkPath = "D:\Tools\emsdk"
}
if (-not $EmsdkPath -or -not (Test-Path $EmsdkPath)) {
    throw "Emsdk was not found. Pass -EmsdkPath pointing to an activated Emscripten 4.0.10 installation."
}
$EmsdkPath = (Resolve-Path $EmsdkPath).Path

$EmsdkPython = $null
if ($env:EMSDK_PYTHON -and (Test-Path $env:EMSDK_PYTHON)) {
    $EmsdkPython = (Resolve-Path $env:EMSDK_PYTHON).Path
}
if (-not $EmsdkPython) {
    $EmsdkPython = Get-ChildItem (Join-Path $EmsdkPath "python\*\python.exe") -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

$EmsdkNode = $null
if ($env:EMSDK_NODE -and (Test-Path $env:EMSDK_NODE)) {
    $EmsdkNode = (Resolve-Path $env:EMSDK_NODE).Path
}
if (-not $EmsdkNode) {
    $NodePatterns = @(
        (Join-Path $EmsdkPath "node\*\bin\node.exe"),
        (Join-Path $EmsdkPath "node\*\node.exe")
    )
    $EmsdkNode = Get-ChildItem -Path $NodePatterns -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
$Emcc = Join-Path $EmsdkPath "upstream\emscripten\emcc.bat"

foreach ($Required in @($EmsdkPython, $EmsdkNode, $Emcc, $Requirements, $BuildScript)) {
    if (-not $Required -or -not (Test-Path $Required)) {
        throw "Required build dependency is missing: $Required"
    }
}

New-Item -ItemType Directory -Force -Path $ToolRoot | Out-Null

if (-not (Test-Path $VenvPython)) {
    & $EmsdkPython -m venv $VenvRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create the pinned SCons environment."
    }
}
if (-not (Test-Path $SConsWheel)) {
    Invoke-WebRequest -Uri $SConsWheelUrl -OutFile $SConsWheel
}
$ActualSConsHash = (Get-FileHash $SConsWheel -Algorithm SHA256).Hash
if ($ActualSConsHash -ne $SConsWheelSha256) {
    Remove-Item $SConsWheel -Force
    throw "SCons wheel SHA-256 mismatch: $ActualSConsHash"
}
& $VenvPython -m pip install --disable-pip-version-check --require-hashes --no-deps --no-index --find-links $ToolRoot -r $Requirements
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $SCons)) {
    throw "Failed to install SCons 4.10.1."
}

$BrotliUrl = "https://github.com/google/brotli/releases/download/v1.2.0/brotli-x64-windows-static.zip"
$BrotliSha256 = "3208AB82D4F08E062A25660CF38092DB4E8415792AA1E579956D37740F8D8924"
if (-not (Test-Path $Brotli)) {
    $Archive = Join-Path $ToolRoot "brotli-x64-windows-static.zip"
    Invoke-WebRequest -Uri $BrotliUrl -OutFile $Archive
    $ActualHash = (Get-FileHash $Archive -Algorithm SHA256).Hash
    if ($ActualHash -ne $BrotliSha256) {
        Remove-Item $Archive -Force
        throw "Brotli archive SHA-256 mismatch: $ActualHash"
    }
    New-Item -ItemType Directory -Force -Path $BrotliRoot | Out-Null
    Expand-Archive -Path $Archive -DestinationPath $BrotliRoot -Force
    Remove-Item $Archive -Force
}

$env:EMSDK = $EmsdkPath
$env:EM_CONFIG = Join-Path $EmsdkPath ".emscripten"
$env:EMSDK_PYTHON = $EmsdkPython
$env:EMSDK_NODE = $EmsdkNode
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
$PathEntries = @(
    $BrotliRoot,
    (Split-Path $EmsdkNode -Parent),
    (Join-Path $EmsdkPath "upstream\emscripten"),
    (Join-Path $EmsdkPath "upstream\bin"),
    (Split-Path $SCons -Parent)
)
$env:PATH = ($PathEntries -join [IO.Path]::PathSeparator) + [IO.Path]::PathSeparator + $env:PATH

Write-Host "Emsdk:  $EmsdkPath"
Write-Host "Python: $VenvPython"
Write-Host "SCons:  $SCons"
Write-Host "Brotli: $Brotli"
Write-Host "Profile: adapter/configs/wechat_2d.py"
Write-Host "Variant: glx-2d-r$Revision"
Write-Host "Tests: disabled"

$BuildArguments = @($BuildScript, "--scons", $SCons, "--revision", "$Revision")
if ($Incremental) {
    $BuildArguments += "--incremental"
}

Push-Location $RepoRoot
try {
    & $VenvPython @BuildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "WeChat GLX build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

Write-Host "Build output: $(Join-Path $RepoRoot 'dist')"
