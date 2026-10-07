# Dot-source this script to prepare only the current process environment.
[CmdletBinding()]
param([string]$SdkDirectory = '')
$ErrorActionPreference = 'Stop'
$devRepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SdkDirectory) {
    if ($env:VULKAN_SDK -and (Test-Path -LiteralPath (Join-Path $env:VULKAN_SDK 'Bin/spirv-val.exe'))) {
        $SdkDirectory = $env:VULKAN_SDK
    } else {
        $SdkDirectory = Join-Path $devRepoRoot 'out/tools/vulkan/1.4.350.0'
    }
}
foreach ($devSdkFile in @('Include/vulkan/vulkan.h', 'Lib/vulkan-1.lib', 'Bin/glslangValidator.exe', 'Bin/spirv-val.exe', 'Bin/VkLayer_khronos_validation.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $SdkDirectory $devSdkFile))) {
        throw "Vulkan SDK incomplete: $devSdkFile. See docs/VALIDATION-WINDOWS.md or supply -SdkDirectory."
    }
}
$devVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $devVsWhere)) { throw 'Visual Studio C++ Build Tools / vswhere missing.' }
$devVsRoot = & $devVsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $devVsRoot) { throw 'No Visual Studio installation with the x64 C++ toolchain found.' }
& (Join-Path $devVsRoot 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo
$env:VULKAN_SDK = [IO.Path]::GetFullPath($SdkDirectory)
$env:PATH = (Join-Path $env:VULKAN_SDK 'Bin') + ';' + $env:PATH
# Copy-only SDKs have no registry entry. Explicit layers are discovered through this process variable.
$env:VK_ADD_LAYER_PATH = (Join-Path $env:VULKAN_SDK 'Bin') + $(if ($env:VK_ADD_LAYER_PATH) { ';' + $env:VK_ADD_LAYER_PATH })
$devCmakeRoot = Join-Path $devVsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake'
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $env:PATH = (Join-Path $devCmakeRoot 'CMake/bin') + ';' + $env:PATH
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $env:PATH = (Join-Path $devCmakeRoot 'Ninja') + ';' + $env:PATH
}
foreach ($devTool in @('cl', 'cmake', 'ctest', 'ninja', 'spirv-val', 'glslangValidator')) {
    if (-not (Get-Command $devTool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $devTool" }
}
Write-Host "Windows x64 tools ready; Vulkan SDK: $env:VULKAN_SDK"
