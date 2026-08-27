[CmdletBinding()]
param(
    [ValidateSet("editor", "template_debug")]
    [string]$Target = "editor",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$AddonSource = Join-Path $RepoRoot "plugin\addon"
$DemoAddons = Join-Path $RepoRoot "demo\addons"
$DemoAddon = Join-Path $DemoAddons "godot-minigame"
$BuiltLibrary = Join-Path $RepoRoot "dist\plugin\native\windows\godot-minigame.windows.x86_64.dll"
$AddonLibrary = Join-Path $DemoAddon "bin\windows\godot-minigame.windows.x86_64.dll"

if (-not $SkipBuild) {
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
}

if (-not (Test-Path -LiteralPath $BuiltLibrary -PathType Leaf)) {
    throw "Built library not found: $BuiltLibrary"
}

if (Test-Path -LiteralPath $DemoAddon) {
    Remove-Item -LiteralPath $DemoAddon -Recurse -Force
}
New-Item -ItemType Directory -Path $DemoAddon -Force | Out-Null
Copy-Item -Path (Join-Path $AddonSource "*") -Destination $DemoAddon -Recurse -Force
New-Item -ItemType Directory -Path (Split-Path $AddonLibrary -Parent) -Force | Out-Null
Copy-Item -LiteralPath $BuiltLibrary -Destination $AddonLibrary -Force

Write-Host "Demo addon assembled at: $DemoAddon"
Write-Host "Native DLL: $AddonLibrary"
Write-Host "Open: $(Join-Path $RepoRoot 'demo\project.godot')"
