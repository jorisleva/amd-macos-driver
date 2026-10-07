[CmdletBinding()]
param([string]$OutputDirectory = '', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'out/translator' }
$manifest = Join-Path $repoRoot 'translator/translator/Cargo.toml'
foreach ($tool in @('cargo', 'rustc', 'spirv-val')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $tool" }
}
& cargo build --locked --manifest-path $manifest --features serde --bin metal2vulkan --target-dir $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw "Translator build failed: $LASTEXITCODE" }
if (-not $SkipTests) {
    # Select the self-contained cases; every_public_fixture* needs the absent corpus.
    Write-Host 'Targeted tests only: excluding every_public_fixture* (unpublished corpus).'
    & cargo test --locked --manifest-path $manifest --features serde --target-dir $OutputDirectory `
        --test deterministic_output --test reflection_describes_each_resource_once `
        --test reflection_access_covers_the_module --test env_registry --test apple_vector_add `
        -- --skip every_public_fixture
    if ($LASTEXITCODE -ne 0) { throw "Translator software tests failed: $LASTEXITCODE" }
}
$fixtureOut = Join-Path $OutputDirectory 'fixture'
[void](New-Item -ItemType Directory -Path $fixtureOut -Force)
$binary = Join-Path $OutputDirectory 'debug/metal2vulkan.exe'
& $binary (Join-Path $repoRoot 'tests/shaders/vector_add.synthetic.ll') (Join-Path $fixtureOut 'vector_add.spv') `
    --stage kernel --local 64,1,1 --whole-workgroups --emit-meta (Join-Path $fixtureOut 'vector_add.reflection.json')
if ($LASTEXITCODE -ne 0) { throw 'Synthetic vector-add translation failed.' }
