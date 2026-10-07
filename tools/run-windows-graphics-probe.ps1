[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DeviceId,
    [string]$BuildDirectory = '', [string]$ReportDirectory = '',
    [switch]$WithoutValidation
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot 'out/vulkan' }
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $repoRoot ('reports/local/graphics-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')) }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$ReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
$imageDirectory = Join-Path $ReportDirectory 'images'
[void](New-Item -ItemType Directory -Path $imageDirectory -Force)
& cmake -S (Join-Path $repoRoot 'tests/vulkan') -B $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $BuildDirectory --config Debug
if ($LASTEXITCODE -ne 0) { throw 'Probe build failed.' }
& ctest --test-dir $BuildDirectory -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Probe software tests failed.' }
$exe = Join-Path $BuildDirectory 'Debug/amd_gpu_probe.exe'
if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $BuildDirectory 'amd_gpu_probe.exe' }
$shaders = @('fullscreen.vert', 'texture.frag', 'triangle.vert', 'solid.frag')
foreach ($shader in $shaders) {
    & spirv-val --target-env vulkan1.2 (Join-Path $BuildDirectory ($shader + '.spv'))
    if ($LASTEXITCODE -ne 0) { throw "SPIR-V validation failed: $shader" }
}
$arguments = @('--graphics', '--device-id', $DeviceId, '--shader-directory', $BuildDirectory,
    '--image-directory', $imageDirectory, '--report', (Join-Path $ReportDirectory 'result.json'))
if (-not $WithoutValidation) { $arguments += '--sync-validation' }
# Preserve the report and diagnostics even if a native tool emits stderr on failure.
$ErrorActionPreference = 'Continue'
& $exe @arguments 2>&1 | Tee-Object -FilePath (Join-Path $ReportDirectory 'probe.log') | Out-Null
$probeExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$artifactPaths = @($exe) + @($shaders | ForEach-Object { Join-Path $BuildDirectory ($_ + '.spv') })
$artifacts = foreach ($path in $artifactPaths) {
    [ordered]@{ name = [IO.Path]::GetFileName($path); sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$inputs = foreach ($path in @('tests/vulkan/main.cpp', 'tests/vulkan/offscreen.h', 'tests/vulkan/offscreen_reference.h', 'tests/vulkan/CMakeLists.txt', 'tests/vulkan/shaders/vector_add.comp', 'tools/run-windows-graphics-probe.ps1', 'tools/initialize-windows-dev.ps1', 'tools/prepare-windows-vulkan-sdk.ps1') + @($shaders | ForEach-Object { 'tests/vulkan/shaders/' + $_ })) {
    [ordered]@{ name = $path; sha256 = (Get-FileHash -LiteralPath (Join-Path $repoRoot $path) -Algorithm SHA256).Hash.ToLowerInvariant() }
}
$images = foreach ($path in Get-ChildItem -LiteralPath $imageDirectory -File -Filter '*.rgba' | Sort-Object Name) {
    [ordered]@{ name = $path.Name; bytes = $path.Length; sha256 = (Get-FileHash -LiteralPath $path.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
[ordered]@{
    schema_version = 1; timestamp_utc = [DateTime]::UtcNow.ToString('o')
    code_revision = (& git -C $repoRoot rev-parse HEAD); working_tree = @(& git -C $repoRoot status --porcelain)
    shader_origin = 'glsl-control'; probe_exit_code = $probeExit; synchronization_validation = -not $WithoutValidation
    windows_version = [Environment]::OSVersion.Version.ToString(); msvc_toolset = $env:VCToolsVersion; windows_sdk = $env:WindowsSDKVersion
    cmake = ((& cmake --version) | Out-String).Trim(); spirv_val = ((& spirv-val --version) | Out-String).Trim()
    glslang = ((& glslangValidator --version) | Out-String).Trim()
    artifacts = @($artifacts); inputs = @($inputs); images = @($images)
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ReportDirectory 'provenance.json') -Encoding utf8
$result = Get-Content -Raw -LiteralPath (Join-Path $ReportDirectory 'result.json') | ConvertFrom-Json
Write-Host ("Graphics: {0}; {1} cases; {2} validation errors. Report: {3}" -f $result.status, @($result.graphics.cases).Count, $result.validation_errors, $ReportDirectory)
if ($probeExit -ne 0) { throw "GPU graphics probe failed ($probeExit): $($result.error). See probe.log." }
