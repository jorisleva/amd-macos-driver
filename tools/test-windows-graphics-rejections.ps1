[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DeviceId,
    [string]$BuildDirectory = '', [string]$ReportDirectory = '',
    [string]$AbsentDeviceId = '0x1234'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot 'out/vulkan' }
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $repoRoot ('reports/local/graphics-rejections-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')) }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$ReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
if ([Convert]::ToUInt32($DeviceId, 16) -eq [Convert]::ToUInt32($AbsentDeviceId, 16)) { throw 'Absent ID must differ from the target.' }
$exe = Join-Path $BuildDirectory 'Debug/amd_gpu_probe.exe'
if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $BuildDirectory 'amd_gpu_probe.exe' }
$variants = @('wrong-alpha', 'wrong-triangle')
foreach ($variant in $variants) {
    $directory = Join-Path $ReportDirectory ($variant + '/shaders')
    [void](New-Item -ItemType Directory -Path $directory -Force)
    foreach ($shader in @('fullscreen.vert', 'texture.frag', 'triangle.vert', 'solid.frag')) {
        Copy-Item -LiteralPath (Join-Path $BuildDirectory ($shader + '.spv')) -Destination $directory
    }
}
@'
#version 450
layout(set=0,binding=0) uniform sampler2D sourceTexture;
layout(location=0) out vec4 outputColor;
void main() {
    outputColor = texture(sourceTexture, gl_FragCoord.xy / vec2(textureSize(sourceTexture,0)));
    outputColor.a = 0.0;
}
'@ | Set-Content -Encoding utf8 -LiteralPath (Join-Path $ReportDirectory 'wrong-alpha/shaders/texture.frag')
@'
#version 450
layout(push_constant) uniform Draw { vec4 color; uint shape; } draw;
layout(location=0) out vec4 outputColor;
void main() { outputColor = draw.color.bgra; }
'@ | Set-Content -Encoding utf8 -LiteralPath (Join-Path $ReportDirectory 'wrong-triangle/shaders/solid.frag')
foreach ($entry in @(@('wrong-alpha', 'texture.frag'), @('wrong-triangle', 'solid.frag'))) {
    $source = Join-Path $ReportDirectory ($entry[0] + '/shaders/' + $entry[1])
    & glslangValidator -V --target-env vulkan1.2 $source -o ($source + '.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Negative shader compilation failed.' }
    & spirv-val --target-env vulkan1.2 ($source + '.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Negative shader SPIR-V validation failed.' }
}
$records = foreach ($variant in @('absent-device') + $variants) {
    $directory = Join-Path $ReportDirectory $variant
    [void](New-Item -ItemType Directory -Path $directory -Force)
    $shaderDirectory = if ($variant -eq 'absent-device') { $BuildDirectory } else { Join-Path $directory 'shaders' }
    $pciId = if ($variant -eq 'absent-device') { $AbsentDeviceId } else { $DeviceId }
    $ErrorActionPreference = 'Continue'
    & $exe --graphics --device-id $pciId --shader-directory $shaderDirectory --image-directory (Join-Path $directory 'images') --sync-validation --report (Join-Path $directory 'result.json') 2>&1 | Tee-Object -FilePath (Join-Path $directory 'probe.log') | Out-Null
    $probeExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $result = Get-Content -Raw -LiteralPath (Join-Path $directory 'result.json') | ConvertFrom-Json
    if ($probeExit -eq 0 -or $result.status -ne 'failed' -or $result.validation_errors -ne 0) { throw "Negative test did not reject $variant correctly." }
    if ($variant -eq 'absent-device' -and ($null -ne $result.selected_device -or @($result.graphics.cases).Count -ne 0)) { throw 'Absent GPU test executed graphics.' }
    if ($variant -ne 'absent-device' -and $result.graphics.cases[-1].pixel_mismatches -eq 0) { throw 'Corrupted GPU pixels were accepted.' }
    [ordered]@{ test = $variant; expected_status = 'failed'; actual_status = $result.status; probe_exit_code = $probeExit
        error = $result.error; cases = @($result.graphics.cases).Count; validation_errors = $result.validation_errors
        final_case = if ($variant -eq 'absent-device') { $null } else { $result.graphics.cases[-1] }
    }
}
[ordered]@{ schema_version = 1; timestamp_utc = [DateTime]::UtcNow.ToString('o'); code_revision = (& git -C $repoRoot rev-parse HEAD)
    shader_origin = 'deliberately-corrupted-glsl-control'; tests = @($records)
} | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 -LiteralPath (Join-Path $ReportDirectory 'rejections.json')
Write-Host "PASS: absent GPU, corrupted alpha and corrupted triangle were rejected. Reports: $ReportDirectory"
