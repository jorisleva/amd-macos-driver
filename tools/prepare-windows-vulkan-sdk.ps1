# Copy the pinned official SDK into the project; do not register layers or change the system PATH.
[CmdletBinding()]
param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$sdkVersion = '1.4.350.0'
$sdkExpectedHash = '855b27ba05d2d8119c5114c5d4ff870ca38f2c632b11e1bb9923b9b7e6ecfe7b'
$sdkRepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $sdkRepoRoot 'out/tools' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[void](New-Item -ItemType Directory -Path $OutputDirectory -Force)
$sdkDestination = Join-Path $OutputDirectory "vulkan/$sdkVersion"
$sdkRequiredFiles = @('Include/vulkan/vulkan.h', 'Lib/vulkan-1.lib', 'Bin/spirv-val.exe', 'Bin/glslangValidator.exe', 'Bin/VkLayer_khronos_validation.json')
$sdkMissing = @($sdkRequiredFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $sdkDestination $_)) })
if (-not $sdkMissing.Count) {
    Write-Host "SDK files already present: $sdkDestination"
    return
}
$sdkInstaller = Join-Path $OutputDirectory "vulkansdk-windows-X64-$sdkVersion.exe"
$sdkUrl = "https://sdk.lunarg.com/sdk/download/$sdkVersion/windows/vulkan_sdk.exe"
if (-not (Test-Path -LiteralPath $sdkInstaller)) {
    Invoke-WebRequest -Uri $sdkUrl -OutFile $sdkInstaller
}
$sdkActualHash = (Get-FileHash -LiteralPath $sdkInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sdkActualHash -ne $sdkExpectedHash) { throw 'SDK installer SHA-256 mismatch; extraction refused.' }
[ordered]@{
    version = $sdkVersion; source = $sdkUrl; sha256 = $sdkActualHash
    prepared_at_utc = [DateTime]::UtcNow.ToString('o'); mode = 'copy_only=1'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'vulkan-sdk-download.json') -Encoding utf8
# The Qt extractor also needs its normal temporary user cache. No system installation is requested.
$sdkProcess = Start-Process -FilePath $sdkInstaller -ArgumentList @(
    '--root', ('"' + $sdkDestination + '"'), '--accept-licenses', '--default-answer', '--confirm-command', 'install', 'copy_only=1'
) -WindowStyle Hidden -RedirectStandardOutput (Join-Path $OutputDirectory 'vulkan-install.log') `
    -RedirectStandardError (Join-Path $OutputDirectory 'vulkan-install.err') -Wait -PassThru
if ($sdkProcess.ExitCode -ne 0) { throw "SDK copy failed ($($sdkProcess.ExitCode)); see vulkan-install.log." }
foreach ($sdkFile in $sdkRequiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdkDestination $sdkFile))) { throw "SDK copy incomplete: $sdkFile" }
}
Write-Host "SDK copied: $sdkDestination"
