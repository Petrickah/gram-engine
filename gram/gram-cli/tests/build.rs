//! `gram build`: builds every module and copies each artifact to `build/<project>/<target>/`.
//! A plain build is for the host OS; `--target` picks one other target; `--ci` builds every
//! target each module declares, into `build/<project>_<version>_<target>/`.
//!
//! These tests run the real cargo and cmake (plus a C++ compiler), so they take a few seconds
//! each. Cross-compiling to Windows (mingw) isn't covered here.

mod support;
use support::*;

// --- what gets built, and where it lands --------------------------------------------------------

#[test]
fn builds_a_cargo_executable() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("demo"))));
}

#[test]
fn builds_a_cmake_executable() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("demo"))));
    // cmake's own build directory lives inside the module, one per target.
    assert!(project.exists(&format!("main/build-{}/CMakeCache.txt", host())));
}

#[test]
fn builds_a_cmake_shared_library() {
    let project = Sandbox::project("demo", &["--lang", "cmake", "--kind", "library"]);

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/{}", host(), shared_lib("main"))));
}

#[test]
fn builds_a_cargo_dynamic_library_next_to_the_executable() {
    // A "dynamic" cargo module is a cdylib the executable loads at runtime: it lands in the
    // build directory under the name cargo gives it (snake_case), not the module's.
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "game-logic", "--kind", "dynamic"]).succeeds();
    assert_contains(&project.read("game-logic/Cargo.toml"), "crate-type = [\"cdylib\"]");

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("demo"))));
    assert!(project.exists(&format!("build/demo/{}/{}", host(), shared_lib("game_logic"))));
}

#[test]
fn finds_a_cmake_library_built_as_static() {
    // The CMakeLists decides SHARED vs STATIC, not gram; gram has to find whichever it is.
    let project = Sandbox::project("demo", &["--lang", "cmake", "--kind", "library"]);
    project.edit("main/CMakeLists.txt", "add_library(main SHARED", "add_library(main STATIC");

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/libmain.a", host())));
}

#[test]
fn builds_a_referenced_library_as_part_of_the_executable() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();
    project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();
    // Make main actually use the library, so a broken link would fail the build.
    project.write("main/src/main.cpp", "#include \"engine/engine.hpp\"\n\nint main() {\n    engine::hello();\n    return 0;\n}\n");

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("demo"))));
    assert!(project.exists(&format!("build/demo/{}/{}", host(), shared_lib("engine"))));
}

#[test]
fn copies_compiled_shaders_next_to_the_executable() {
    // Anything a cmake module puts in <build dir>/shaders/ is a runtime asset and travels
    // with the executable. Here the CMakeLists writes a fake one at configure time.
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    let cmakelists = project.read("main/CMakeLists.txt");
    project.write("main/CMakeLists.txt", &format!("{cmakelists}\nfile(WRITE ${{CMAKE_BINARY_DIR}}/shaders/Default.vert.spv \"spirv\")\n"));

    project.gram(&["build"]).succeeds();

    assert_eq!(project.read(&format!("build/demo/{}/shaders/Default.vert.spv", host())), "spirv");
}

#[test]
fn builds_a_cargo_workspace_member() {
    // A workspace builds into *its* target/ directory and names the binary after the package,
    // not after the gram module. gram asks cargo where the binary is instead of guessing.
    let project = Sandbox::project("demo", &[]);
    project.write("main/Cargo.toml", "[workspace]\nresolver = \"2\"\nmembers = [\"app\"]\n");
    project.write("main/app/Cargo.toml", "[package]\nname = \"app\"\nversion = \"0.1.0\"\nedition = \"2024\"\n");
    project.write("main/app/src/main.rs", "fn main() { println!(\"from the workspace\"); }\n");

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("demo"))),
        "the artifact is named after the project (main is its main module), not the cargo package");
}

#[test]
fn only_the_main_runtime_module_is_named_after_the_project() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "tool", "--lang", "cmake"]).succeeds();

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("demo"))));
    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("tool"))), "not the main module: keeps its name");
}

// --- targets ------------------------------------------------------------------------------------

#[test]
fn ci_builds_every_declared_target_into_versioned_directories() {
    let project = Sandbox::project("demo", &[]); // declares just the host

    project.gram(&["build", "--ci"]).succeeds();

    assert!(project.exists(&format!("build/demo_0.1.0_{}/{}", host(), exe("demo"))));
}

#[test]
fn ci_skips_targets_this_host_cannot_build() {
    let other = unbuildable_target();
    let project = Sandbox::project("demo", &["--target", &format!("{},{other}", host())]);

    let run = project.gram(&["build", "--ci"]).succeeds();

    assert!(run.printed(&format!("skipping module main target \"{other}\"")));
    assert!(project.exists(&format!("build/demo_0.1.0_{}/{}", host(), exe("demo"))));
    assert!(!project.exists(&format!("build/demo_0.1.0_{other}")));
}

#[test]
fn ci_and_target_cannot_be_combined() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--ci", "--target", host()])
        .fails_with("--target can't be combined with --ci");
}

#[test]
fn rejects_an_unknown_target() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--target", "playstation"])
        .fails_with("unsupported target: playstation");
}

#[test]
fn refuses_a_target_this_host_cannot_build() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--target", unbuildable_target()])
        .fails_with(&format!("can't build target {} from a {} host", unbuildable_target(), host()));
}

#[test]
fn warns_when_building_a_target_the_module_does_not_declare() {
    // Only a warning: the build still happens.
    let project = Sandbox::project("demo", &["--target", unbuildable_target()]);

    let run = project.gram(&["build"]).succeeds();

    assert!(run.printed(&format!("does not declare \"{}\"", host())));
    assert!(project.exists(&format!("build/demo/{}/{}", host(), exe("demo"))));
}

// --- vcpkg ----------------------------------------------------------------------------------------

#[test]
fn a_vcpkg_module_needs_vcpkg_root() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "glm", "--module", "main", "--vcpkg"]).succeeds();

    project.gram(&["build"]).fails_with("VCPKG_ROOT isn't set");
}

#[test]
fn merges_the_vcpkg_manifests_of_referenced_modules() {
    // vcpkg only reads the top-level project's vcpkg.json, so main's build has to carry
    // engine's vcpkg dependencies too. gram writes a merged manifest into main's build dir.
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();
    project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();
    // Written by hand (not `gram add --vcpkg`, which would also add a find_package that the
    // fake vcpkg below can't satisfy). Both ask for imgui, with different features.
    project.write("engine/vcpkg.json", r#"{ "name": "engine", "version": "0.1.0", "dependencies": [ { "name": "imgui", "features": ["docking"] }, "glm" ] }"#);
    project.write("main/vcpkg.json", r#"{ "name": "main", "version": "0.1.0", "dependencies": [ "glm", { "name": "imgui", "features": ["vulkan-binding"] } ] }"#);
    let vcpkg_root = fake_vcpkg_root(&project);

    project.gram_with_env(&["build"], &[("VCPKG_ROOT", &display(&vcpkg_root))]).succeeds();

    let merged = project.read(&format!("main/build-{}/vcpkg-manifest/vcpkg.json", host()));
    assert_contains(&merged, r#""name": "main""#);
    assert_eq!(merged.matches(r#""glm""#).count(), 1, "a port both modules use appears once:\n{merged}");
    assert_eq!(merged.matches(r#""imgui""#).count(), 1, "{merged}");
    assert_contains(&merged, r#""docking""#);
    assert_contains(&merged, r#""vulkan-binding""#);
    assert!(!project.exists(&format!("engine/build-{}/vcpkg-manifest", host())),
        "engine references nothing, so its own vcpkg.json is used as-is");
}

// --- failures -----------------------------------------------------------------------------------

#[test]
fn reports_a_cargo_compile_error() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/src/main.rs", "fn main() { let broken: u32 = \"not a number\"; }\n");

    let run = project.gram(&["build"]).fails_with("cargo build failed");

    assert!(run.printed("mismatched types"), "cargo's own diagnostic is shown, not swallowed");
}

#[test]
fn reports_a_cmake_compile_error() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.write("main/src/main.cpp", "int main() { return not_declared; }\n");

    project.gram(&["build"]).fails_with("cmake build failed");
}
