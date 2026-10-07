#!/bin/bash
# Run on a Mac; produce an authored Apple AIR corpus for the Windows probe.
set -euo pipefail
export LC_ALL=C
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$repo_root/out/metal-reference}"
translator="${METAL2VULKAN_BIN:-$repo_root/out/translator/debug/metal2vulkan}"
air_target="${METAL_REFERENCE_TARGET:-air64-apple-macosx26.0}"
metal_std="${METAL_REFERENCE_STD:-metal4.0}"
[ "$(uname -s)" = Darwin ] || { echo 'Apple Metal compilation requires macOS.' >&2; exit 2; }
for tool in xcrun python3 rustc git shasum; do
  command -v "$tool" >/dev/null || { echo "Missing: $tool" >&2; exit 2; }
done
[ -x "$translator" ] || { echo "Build the translator first: $translator" >&2; exit 2; }
llvm_dis="$(command -v "${METAL2VULKAN_LLVM_DIS:-llvm-dis}")" || { echo 'Missing: llvm-dis (or METAL2VULKAN_LLVM_DIS).' >&2; exit 2; }
spirv_val="$(command -v "${METAL2VULKAN_SPIRV_VAL:-spirv-val}")" || { echo 'Missing: spirv-val (or METAL2VULKAN_SPIRV_VAL).' >&2; exit 2; }
spirv_dis="$(command -v "${METAL2VULKAN_SPIRV_DIS:-spirv-dis}")" || { echo 'Missing: spirv-dis (or METAL2VULKAN_SPIRV_DIS).' >&2; exit 2; }
export METAL2VULKAN_LLVM_DIS="$llvm_dis" METAL2VULKAN_SPIRV_VAL="$spirv_val"
metal_bin="$(xcrun --sdk macosx --find metal)"
metallib_bin="$(xcrun --sdk macosx --find metallib)"
xcrun --sdk macosx metal --version >/dev/null || { echo 'Install the Metal toolchain: xcodebuild -downloadComponent MetalToolchain' >&2; exit 2; }
# A failed/repeated invocation must never present stale outputs as new evidence.
if [ -e "$out" ] && { [ ! -d "$out" ] || [ -n "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" ]; }; then
  echo "Choose a new or empty output directory: $out" >&2
  exit 2
fi
mkdir -p "$out"
out="$(cd "$out" && pwd)"
exec > >(tee "$out/build.log") 2>&1
trap 'code=$?; if [ "$code" -ne 0 ]; then printf "failed (exit %s)\n" "$code" > "$out/status.txt"; fi' EXIT
cp "$repo_root/tests/shaders/vector_add.metal" "$out/vector_add.metal"
# AIR64 is 64-bit GPU IR, NOT an x86_64 host executable. The OS target is Tahoe.
# stdin avoids Apple's absolute !air.source_file_name metadata (prefix-map does
# not remap it in Metal 32023.864). Preserve the exact input beside the AIR.
xcrun --sdk macosx metal -x metal -O2 -target "$air_target" -std="$metal_std" -c - \
  -o "$out/vector_add.air" < "$out/vector_add.metal"
"$llvm_dis" "$out/vector_add.air" -o "$out/vector_add.air.ll"
xcrun --sdk macosx metallib "$out/vector_add.air" -o "$out/vector_add.metallib"
"$translator" "$out/vector_add.air" "$out/vector_add.spv" --stage kernel \
  --local 64,1,1 --whole-workgroups --emit-meta "$out/vector_add.reflection.json"
"$spirv_val" --target-env vulkan1.2 "$out/vector_add.spv"
"$spirv_dis" "$out/vector_add.spv" -o "$out/vector_add.spvasm"
# llvm-dis embeds its input path in a comment, not in Apple's AIR. Strip only
# that generated comment from the diagnostic text; the AIR is never rewritten.
python3 - "$repo_root" "$out" "$translator" "$llvm_dis" "$spirv_val" "$spirv_dis" "$metal_bin" "$metallib_bin" "$air_target" "$metal_std" <<'PY'
import datetime
import hashlib
import json
import platform
import re
import subprocess
import sys
from pathlib import Path

repo, out = map(Path, sys.argv[1:3])
translator, llvm_dis, spirv_val, spirv_dis, metal_bin, metallib_bin, target, std = sys.argv[3:]

def run(*args):
    return subprocess.check_output(args, text=True).strip()

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

ll_path = out / 'vector_add.air.ll'
ll = '\n'.join(line for line in ll_path.read_text().splitlines() if not line.startswith('; ModuleID =')) + '\n'
ll_path.write_text(ll)
meta = json.loads((out / 'vector_add.reflection.json').read_text())
asm = (out / 'vector_add.spvasm').read_text()
resources = sorted(meta['bindings'], key=lambda item: item['metal_index'])
valid = (meta['reflection_version'] == 56 and meta['stage'] == 'Kernel'
         and meta['entry_point'] == 'vector_add' and meta['local_size'] == [64, 1, 1]
         and meta['kernel_dispatch'] == 'Workgroups' and len(resources) == 4)
for index, item in enumerate(resources):
    valid = valid and (item['kind'] == 'Buffer' and item['metal_index'] == index
                      and item['descriptor'] == {'set': 0, 'binding': index, 'count': 1}
                      and item['access'] == ('WriteOnly' if index == 2 else 'ReadOnly'))
valid = valid and resources[-1]['declared_size'] == 8 and resources[-1]['type_layout'] == {
    'Struct': [{'offset': 0, 'ty': {'Scalar': 'UInt'}}, {'offset': 4, 'ty': {'Scalar': 'UInt'}}]}
valid = valid and len(re.findall(r'OpEntryPoint GLCompute %\S+ "main"', asm)) == 1
valid = valid and len(re.findall(r'OpExecutionMode %\S+ LocalSize 64 1 1\b', asm)) == 1
valid = valid and sorted(map(int, re.findall(r'OpDecorate %\S+ Binding (\d+)', asm))) == [0, 1, 2, 3]
valid = valid and re.findall(r'OpDecorate %\S+ DescriptorSet (\d+)', asm) == ['0'] * 4
if not valid:
    raise SystemExit('Reflection/SPIR-V does not match the vector-add Windows probe ABI.')
# Reject personal paths rather than silently sanitizing the compiled bitcode.
for name in ('vector_add.air', 'vector_add.metallib', 'vector_add.air.ll', 'vector_add.spvasm', 'vector_add.reflection.json'):
    if str(repo).encode() in (out / name).read_bytes() or b'/Users/' in (out / name).read_bytes():
        raise SystemExit(f'Personal source path remains in {name}; do not publish this corpus.')

names = ['vector_add.metal', 'vector_add.air', 'vector_add.air.ll', 'vector_add.metallib',
         'vector_add.spv', 'vector_add.spvasm', 'vector_add.reflection.json']
inputs = ['tools/compile-metal-reference.sh', 'tools/build-translator.sh',
          'tests/shaders/vector_add.metal', 'tests/shaders/vector_add.synthetic.ll',
          'translator/translator/Cargo.toml', 'translator/translator/Cargo.lock']
# Identify the complete translator source snapshot, including uncommitted edits.
source_names = run('git', '-C', str(repo), 'ls-files', '--cached', '--others', '--exclude-standard', 'translator/translator').splitlines()
source_fingerprint = hashlib.sha256(''.join(f'{name}\0{sha(repo / name)}\n' for name in sorted(source_names)).encode()).hexdigest()
versions = {
    'metal': '\n'.join(line for line in run('xcrun', '--sdk', 'macosx', 'metal', '--version').splitlines()
                       if not line.startswith('InstalledDir:')),
    'metallib': run('xcrun', '--sdk', 'macosx', 'metallib', '--version'),
    'llvm_dis': run(llvm_dis, '--version'), 'spirv_val': run(spirv_val, '--version'),
    'spirv_dis': run(spirv_dis, '--version'), 'rustc': run('rustc', '--version'),
    'xcode': run('xcodebuild', '-version'), 'macos_sdk': run('xcrun', '--sdk', 'macosx', '--show-sdk-version')}
provenance = {
    'schema_version': 1, 'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'code_revision': run('git', '-C', str(repo), 'rev-parse', 'HEAD'),
    'working_tree': run('git', '-C', str(repo), 'status', '--porcelain').splitlines(),
    'machine': {'model': run('sysctl', '-n', 'hw.model'), 'architecture': platform.machine(),
                'cpu': run('sysctl', '-n', 'machdep.cpu.brand_string'),
                'ram_bytes': int(run('sysctl', '-n', 'hw.memsize')),
                'macos_version': run('sw_vers', '-productVersion'), 'macos_build': run('sw_vers', '-buildVersion')},
    'versions': versions,
    'tools': [{'name': name, 'sha256': sha(path), 'format': run('file', '-b', path).splitlines()[0]}
              for name, path in [('metal_launcher', metal_bin), ('metallib_launcher', metallib_bin),
                                 ('metal2vulkan', translator), ('llvm-dis', llvm_dis),
                                 ('spirv-val', spirv_val), ('spirv-dis', spirv_dis)]],
    'options': {'air_target': target, 'metal_standard': std, 'optimization': '-O2',
                'source_input': 'stdin (exact vector_add.metal copy)', 'stage': 'kernel',
                'local_size': [64, 1, 1], 'dispatch': 'Workgroups', 'validation_target': 'vulkan1.2'},
    'air_triple': re.search(r'target triple = "([^"]+)"', ll).group(1),
    'contract': {'reflection_version': 56, 'spirv_entry_point': 'main', 'air_entry_point': meta['entry_point'],
                 'descriptor_set': 0, 'storage_buffer_bindings': [0, 1, 2, 3],
                 'requires_shader_int64': 'OpCapability Int64' in asm, 'params_size_bytes': 8},
    'results': {'apple_air': 'passed', 'metallib': 'passed', 'llvm_dis': 'passed',
                'translation': 'passed', 'spirv_validation': 'passed', 'probe_abi_check': 'passed',
                'metal_gpu_execution': 'not-run', 'radeon_gpu_execution': 'not-run'},
    'translator_sources_sha256': source_fingerprint,
    'inputs': [{'name': name, 'sha256': sha(repo / name)} for name in inputs],
    'artifacts': [{'name': name, 'sha256': sha(out / name), 'size_bytes': (out / name).stat().st_size} for name in names]}
(out / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
print('ABI: schema 56, Kernel, Workgroups, local 64x1x1, buffers 0..3, entry main: PASS')
PY
# Relative filenames allow integrity verification after copying to Windows.
(cd "$out" && shasum -a 256 vector_add.metal vector_add.air vector_add.air.ll \
  vector_add.metallib vector_add.spv vector_add.spvasm vector_add.reflection.json provenance.json > SHA256SUMS)
printf 'passed (compilation and software checks only; no GPU execution)\n' > "$out/status.txt"
echo "Artifacts: $out (verify SHA256SUMS before executing on the Radeon)."
