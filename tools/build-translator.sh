#!/bin/bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
target_dir="${1:-$repo_root/out/translator}"
for tool in cargo rustc spirv-val; do command -v "$tool" >/dev/null || { echo "Missing: $tool" >&2; exit 2; }; done
cargo build --locked --manifest-path "$repo_root/translator/translator/Cargo.toml" \
  --features serde --bin metal2vulkan --target-dir "$target_dir"
cargo test --locked --manifest-path "$repo_root/translator/translator/Cargo.toml" \
  --features serde --target-dir "$target_dir" --test deterministic_output \
  --test reflection_describes_each_resource_once --test reflection_access_covers_the_module --test env_registry \
  -- --skip every_public_fixture
mkdir -p "$target_dir/fixture"
"$target_dir/debug/metal2vulkan" "$repo_root/tests/shaders/vector_add.synthetic.ll" \
  "$target_dir/fixture/vector_add.spv" --stage kernel --local 64,1,1 --whole-workgroups \
  --emit-meta "$target_dir/fixture/vector_add.reflection.json"
