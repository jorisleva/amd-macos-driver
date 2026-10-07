#!/bin/bash
# Run on the Mac; preserve AIR, library, reflection and hashes for transfer to Windows.
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$repo_root/out/metal-reference}"
translator="${METAL2VULKAN_BIN:-$repo_root/out/translator/debug/metal2vulkan}"
mkdir -p "$out"
command -v xcrun >/dev/null || { echo 'Xcode Metal tools are required on macOS.' >&2; exit 2; }
[ -x "$translator" ] || { echo "Build the translator first: $translator" >&2; exit 2; }
# The AIR targets the intended x86_64 host, even when compilation runs on Apple Silicon.
xcrun -sdk macosx metal -target air64-apple-macosx26.0 -c \
  "$repo_root/tests/shaders/vector_add.metal" -o "$out/vector_add.air"
xcrun -sdk macosx metallib "$out/vector_add.air" -o "$out/vector_add.metallib"
"$translator" "$out/vector_add.air" "$out/vector_add.spv" --stage kernel \
  --local 64,1,1 --whole-workgroups --emit-meta "$out/vector_add.reflection.json"
spirv-val --target-env vulkan1.2 "$out/vector_add.spv"
{
  git -C "$repo_root" rev-parse HEAD
  git -C "$repo_root" status --porcelain
  sw_vers
  xcodebuild -version
  xcrun --sdk macosx --show-sdk-version
  xcrun -sdk macosx metal --version
  rustc --version
  spirv-val --version
  printf 'translator sha256: '
  shasum -a 256 "$translator"
} > "$out/provenance.txt"
shasum -a 256 "$repo_root/tests/shaders/vector_add.metal" "$out/vector_add.air" \
  "$out/vector_add.metallib" "$out/vector_add.spv" "$out/vector_add.reflection.json" > "$out/SHA256SUMS"
echo "Artifacts: $out (inspect reflection before GPU execution; no Metal execution performed here)."
