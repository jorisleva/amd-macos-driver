[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DeviceId,
    [string]$BuildDirectory = '', [string]$ReportDirectory = '',
    [string]$Shader = '', [string]$Reflection = '',
    [ValidateSet('metal-air', 'synthetic-ir')][string]$ShaderOrigin = 'metal-air',
    [switch]$WithoutValidation
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot 'out/vulkan' }
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $repoRoot ('reports/local/probe-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')) }
[void](New-Item -ItemType Directory -Path $ReportDirectory -Force)
& cmake -S (Join-Path $repoRoot 'tests/vulkan') -B $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $BuildDirectory --config Debug
if ($LASTEXITCODE -ne 0) { throw 'Probe build failed.' }
& ctest --test-dir $BuildDirectory -C Debug --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Probe software tests failed.' }
$exe = Join-Path $BuildDirectory 'Debug/amd_gpu_probe.exe'
if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $BuildDirectory 'amd_gpu_probe.exe' }
$shaderKind = 'glsl-control'
$entryPoint = 'main'
if (-not $Shader) { $Shader = Join-Path $BuildDirectory 'vector_add.spv' }
else {
    $shaderKind = $ShaderOrigin
    if (-not $Reflection) { throw 'An external shader requires its translator reflection JSON.' }
    $meta = Get-Content -Raw -LiteralPath $Reflection | ConvertFrom-Json
    $bindingIndices = @($meta.bindings | ForEach-Object { $_.descriptor.binding } | Sort-Object)
    if ($meta.reflection_version -ne 56 -or $meta.stage -ne 'Kernel' -or $meta.kernel_dispatch -ne 'Workgroups' -or
        ($meta.local_size -join ',') -ne '64,1,1' -or ($bindingIndices -join ',') -ne '0,1,2,3' -or
        @($meta.bindings | Where-Object { $_.kind -ne 'Buffer' -or $_.descriptor.set -ne 0 -or $_.descriptor.count -ne 1 }).Count) {
        throw 'Shader reflection does not match the vector-add probe contract (schema 56, four buffers, set 0, local 64, Workgroups).'
    }
    if (-not $meta.entry_point) { throw 'Reflection has no entry point.' }
    # Reflection names the AIR function. The translator emits SPIR-V entry point "main".
    $entryPoint = 'main'
}
& spirv-val --target-env vulkan1.2 $Shader
if ($LASTEXITCODE -ne 0) { throw 'SPIR-V validation failed.' }
$arguments = @('--report', (Join-Path $ReportDirectory 'result.json'), '--device-id', $DeviceId, '--shader', $Shader, '--entry', $entryPoint)
if (-not $WithoutValidation) { $arguments += '--validation' }
& $exe @arguments
$probeExit = $LASTEXITCODE
$artifacts = foreach ($path in @($exe, $Shader, $Reflection) | Where-Object { $_ }) {
    $hash = Get-FileHash -LiteralPath $path -Algorithm SHA256
    [ordered]@{ name = [IO.Path]::GetFileName($path); sha256 = $hash.Hash.ToLowerInvariant() }
}
# Include hashes of all build inputs so uncommitted edits are distinguishable without personal paths.
$inputs = foreach ($path in @('tests/vulkan/main.cpp', 'tests/vulkan/CMakeLists.txt', 'tests/vulkan/shaders/vector_add.comp', 'tests/shaders/vector_add.metal', 'tests/shaders/vector_add.synthetic.ll', 'tools/run-windows-probe.ps1')) {
    [ordered]@{ name = $path; sha256 = (Get-FileHash -LiteralPath (Join-Path $repoRoot $path) -Algorithm SHA256).Hash.ToLowerInvariant() }
}
[ordered]@{
    schema_version = 1; timestamp_utc = [DateTime]::UtcNow.ToString('o')
    code_revision = (& git -C $repoRoot rev-parse HEAD); working_tree = @(& git -C $repoRoot status --porcelain)
    shader_origin = $shaderKind; probe_exit_code = $probeExit
    windows_version = [Environment]::OSVersion.Version.ToString()
    cmake = ((& cmake --version) | Out-String).Trim(); spirv_val = ((& spirv-val --version) | Out-String).Trim()
    artifacts = @($artifacts); inputs = @($inputs)
    hardware_inventory = 'Collect separately with tools/collect-windows-inventory.ps1 -Role target'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ReportDirectory 'provenance.json') -Encoding utf8
Write-Host "Report: $ReportDirectory"
if ($probeExit -ne 0) { throw "GPU probe failed ($probeExit); see result.json." }
