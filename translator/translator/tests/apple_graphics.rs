#![cfg(feature = "serde")]

use std::path::Path;
use std::process::Command;

// Our authored Apple AIR disassemblies, not private/system shaders. Each CLI
// invocation selects the direct descriptor ABI before any OnceLock initializes.
// Byte/reflection stability is a software check, not GPU pixel equivalence.
fn check(name: &str, stage: &str) {
    let corpus = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/shaders/apple/graphics");
    let expected = std::fs::read(corpus.join(format!("{name}.spv"))).expect("committed SPIR-V");
    let expected_meta: serde_json::Value = serde_json::from_slice(
        &std::fs::read(corpus.join(format!("{name}.reflection.json"))).expect("committed reflection"),
    )
    .expect("reflection JSON");
    let scratch = std::env::temp_dir().join(format!("m2v_apple_graphics_{}_{name}", std::process::id()));
    for _ in 0..3 {
        std::fs::create_dir_all(&scratch).expect("scratch directory");
        let output = Command::new(env!("CARGO_BIN_EXE_metal2vulkan"))
            .env("NVMTL_NO_BINDLESS_ALL", "1")
            .arg(corpus.join(format!("{name}.air.ll")))
            .arg(scratch.join("out.spv"))
            .args(["--stage", stage, "--emit-meta"])
            .arg(scratch.join("out.json"))
            .output()
            .expect("run translator CLI");
        if !output.status.success() {
            let _ = std::fs::remove_dir_all(&scratch);
            panic!("{name}: {}", String::from_utf8_lossy(&output.stderr));
        }
        let actual = std::fs::read(scratch.join("out.spv")).expect("translated SPIR-V");
        let actual_meta: serde_json::Value = serde_json::from_slice(
            &std::fs::read(scratch.join("out.json")).expect("translated reflection"),
        )
        .expect("translated JSON");
        let _ = std::fs::remove_dir_all(&scratch);
        assert_eq!(actual, expected, "{name}: SPIR-V regression");
        assert_eq!(actual_meta, expected_meta, "{name}: reflection regression");
    }
}

#[test]
fn apple_fullscreen_vertex() {
    check("fullscreen.vert", "vertex");
}
#[test]
fn apple_texture_fragment() {
    check("texture.frag", "fragment");
}
#[test]
fn apple_triangle_vertex() {
    check("triangle.vert", "vertex");
}
#[test]
fn apple_solid_fragment() {
    check("solid.frag", "fragment");
}
