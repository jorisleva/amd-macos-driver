#![cfg(feature = "serde")]

use metal2vulkan::passes::{Stage, TransformOptions};
use metal2vulkan::reflect::KernelDispatch;
use metal2vulkan::translate_reflected_with_options;

// Owned shader, compiled by Apple Metal 32023.864 on 2026-10-07. Testing the
// disassembled AIR avoids an LLVM dependency on Windows; spirv-val is mandatory.
// This locks software output, NOT equivalence of results on a GPU.
#[test]
fn authored_apple_air_preserves_vector_add_bytes_and_reflection() {
    let input = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../tests/shaders/apple/vector_add.air.ll"
    );
    let expected_spv = include_bytes!("../../../tests/shaders/apple/vector_add.spv");
    let expected_meta: serde_json::Value = serde_json::from_str(include_str!(
        "../../../tests/shaders/apple/vector_add.reflection.json"
    ))
    .expect("committed reflection JSON");
    let tmp = std::env::temp_dir().join(format!("m2v_apple_vector_add_{}", std::process::id()));
    let options = TransformOptions {
        kernel_local_size: [64, 1, 1],
        kernel_dispatch: Some(KernelDispatch::Workgroups),
        ..TransformOptions::default()
    };
    for _ in 0..3 {
        std::fs::create_dir_all(&tmp).expect("scratch directory");
        let translated = translate_reflected_with_options(input, Stage::Kernel, &tmp, options);
        let _ = std::fs::remove_dir_all(&tmp);
        let (bytes, reflection) = translated.expect("authored Apple AIR must translate and validate");
        assert_eq!(bytes.as_slice(), expected_spv.as_slice(), "SPIR-V regression");
        assert_eq!(
            serde_json::to_value(reflection).expect("serialize reflection"),
            expected_meta,
            "probe ABI / buffer access / layout regression"
        );
    }
}
