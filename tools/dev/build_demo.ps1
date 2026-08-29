[CmdletBinding()]
param(
    [ValidateSet("editor", "template_debug")]
    [string]$Target = "editor"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$BuiltLibrary = Join-Path $RepoRoot "dist\plugin\native\windows\godot-minigame.windows.x86_64.dll"

Push-Location $RepoRoot
try {
    & uvx --from scons==4.9.1 scons `
        -f plugin/build/SConstruct `
        platform=windows `
        arch=x86_64 `
        target=$Target `
        embed_resources=yes
    if ($LASTEXITCODE -ne 0) {
        throw "Native plugin build failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $BuiltLibrary -PathType Leaf)) {
    throw "Built library not found: $BuiltLibrary"
}

Write-Host "Native DLL: $BuiltLibrary"
