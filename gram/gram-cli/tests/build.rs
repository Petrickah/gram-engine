//! `gram build`: builds every module and copies each artifact to `build/<project>/<target>/<config>/`.
//! A plain build is for the host OS; `--target` picks one other target; `--ci` builds every
//! target each module declares, into `build/<project>_<version>_<target>_<config>/`.
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

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
}

#[test]
fn builds_a_cmake_executable() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
    // cmake's own build directory lives inside the module, one per target.
    assert!(project.exists(&format!("main/build-{}-debug/CMakeCache.txt", host())));
}

#[test]
fn builds_a_cmake_shared_library() {
    let project = Sandbox::project("demo", &["--lang", "cmake", "--kind", "library"]);

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), shared_lib("main"))));
}

#[test]
fn builds_a_cargo_dynamic_library_next_to_the_executable() {
    // A "dynamic" cargo module is a cdylib the executable loads at runtime: it lands in the
    // build directory named after the gram module, whatever the crate is called.
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "GameLogic", "--kind", "dynamic"]).succeeds();
    project.edit("GameLogic/Cargo.toml", "name = \"GameLogic\"", "name = \"game-logic\"");
    assert_contains(&project.read("GameLogic/Cargo.toml"), "crate-type = [\"cdylib\"]");

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), shared_lib("GameLogic"))));
    assert!(!project.exists(&format!("build/demo/{}/debug/{}", host(), shared_lib("game_logic"))));

    // How the editor's Play button builds it again: one `<key> <value>` per line.
    let command = project.read(&format!("build/demo/{}/debug/GameLogic.gram-build", host()));
    let args: Vec<&str> = command.lines().filter_map(|line| line.strip_prefix("arg ")).collect();
    assert!(args[0].ends_with(&exe("gram")), "the program first: {command}");
    assert_eq!(args[1..], ["build", "--module", "GameLogic", "--config", "debug"]);
    assert!(command.lines().any(|line| line.starts_with("dir ")));
    assert!(command.lines().any(|line| line.starts_with("env PATH=")));
}

#[test]
fn only_a_dynamic_module_gets_a_rebuild_command() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--config", "release"]).succeeds();

    assert!(!project.exists(&format!("build/demo/{}/release/main.gram-build", host())));
}

#[test]
fn finds_a_cmake_library_built_as_static() {
    // The CMakeLists decides SHARED vs STATIC, not gram; gram has to find whichever it is.
    let project = Sandbox::project("demo", &["--lang", "cmake", "--kind", "library"]);
    project.edit("main/CMakeLists.txt", "add_library(main SHARED", "add_library(main STATIC");

    project.gram(&["build"]).succeeds();

    // MSVC names a static library <name>.lib, GCC/clang (mingw too) lib<name>.a.
    let file = if host() == "windows" { "main.lib" } else { "libmain.a" };
    assert!(project.exists(&format!("build/demo/{}/debug/{file}", host())));
}

#[test]
fn builds_a_referenced_library_as_part_of_the_executable() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();
    project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();
    // Make main actually use the library, so a broken link would fail the build.
    project.write("main/src/main.cpp", "#include \"engine/engine.hpp\"\n\nint main() {\n    engine::hello();\n    return 0;\n}\n");

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), shared_lib("engine"))));
    // Compiled once, inside main's build (add_subdirectory), not a second time on its own.
    assert!(project.exists(&format!("main/build-{}-debug/engine", host())));
    assert!(!project.exists(&format!("engine/build-{}-debug", host())), "engine was compiled twice");
}

#[test]
fn builds_a_referenced_library_on_its_own_when_asked_by_name() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();
    project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();

    project.gram(&["build", "--module", "engine"]).succeeds();

    assert!(project.exists(&format!("engine/build-{}-debug", host())));
    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), shared_lib("engine"))));
}

#[test]
fn copies_compiled_shaders_next_to_the_executable() {
    // Anything a cmake module puts in <build dir>/shaders/ is a runtime asset and travels
    // with the executable. Here the CMakeLists writes a fake one at configure time.
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    let cmakelists = project.read("main/CMakeLists.txt");
    project.write("main/CMakeLists.txt", &format!("{cmakelists}\nfile(WRITE ${{CMAKE_BINARY_DIR}}/shaders/Default.vert.spv \"spirv\")\n"));

    project.gram(&["build"]).succeeds();

    assert_eq!(project.read(&format!("build/demo/{}/debug/shaders/Default.vert.spv", host())), "spirv");
}

#[test]
fn copies_textures_next_to_the_executable() {
    // Same for <build dir>/textures/: the images the game loads at runtime.
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    let cmakelists = project.read("main/CMakeLists.txt");
    project.write("main/CMakeLists.txt", &format!("{cmakelists}\nfile(WRITE ${{CMAKE_BINARY_DIR}}/textures/texture.jpg \"pixels\")\n"));

    project.gram(&["build"]).succeeds();

    assert_eq!(project.read(&format!("build/demo/{}/debug/textures/texture.jpg", host())), "pixels");
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

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))),
        "the artifact is named after the project (main is its main module), not the cargo package");
}

#[test]
fn only_the_main_runtime_module_is_named_after_the_project() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "tool", "--lang", "cmake"]).succeeds();

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("tool"))), "not the main module: keeps its name");
}

#[test]
fn module_builds_only_that_module() {
    // E.g. only the game's Rust library while the game runs: nothing else is rebuilt.
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "tool"]).succeeds();

    project.gram(&["build", "--module", "tool"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("tool"))));
    assert!(!project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
}

#[test]
fn module_must_exist() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--module", "nowhere"]).fails_with("module not found: nowhere");
}

// --- configurations (--config) ------------------------------------------------------------------

#[test]
fn release_lands_in_its_own_folder() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--config", "release"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/release/{}", host(), exe("demo"))));
    assert!(!project.exists(&format!("build/demo/{}/debug", host())));
    // cargo really built with --release.
    assert!(project.exists(&format!("main/target/release/{}", exe("main"))));
}

#[test]
fn cmake_gets_one_build_dir_per_config_with_its_build_type() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["build", "--config", "release"]).succeeds();

    let cache = project.read(&format!("main/build-{}-release/CMakeCache.txt", host()));
    // Release keeps symbols: the compiler's RelWithDebInfo flags (GramConfigurationFlags.cmake).
    // On Windows that's MSVC through the Visual Studio generator, which is multi-config: it ignores the
    // build type (left UNINITIALIZED) and picks the configuration in `cmake --build --config`.
    if host() == "windows" {
        assert_contains(&cache, "CMAKE_CXX_FLAGS_RELEASE:STRING=/Zi /O2 /Ob1 /DNDEBUG");
    } else {
        assert_contains(&cache, "CMAKE_BUILD_TYPE:STRING=Release");
        assert_contains(&cache, "CMAKE_CXX_FLAGS_RELEASE:STRING=-O2 -g -DNDEBUG");
    }
    assert!(project.exists(&format!("build/demo/{}/release/{}", host(), exe("demo"))));
}

#[test]
fn cmake_learns_the_final_config_from_gram() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["build", "--config", "final"]).succeeds();

    // The compiler's own Release flags, plus LTO: CMake knows nothing of a "Final" configuration by itself.
    let cache = project.read(&format!("main/build-{}-final/CMakeCache.txt", host()));
    if host() == "windows" {
        // MSVC: Release's /O2 /Ob2; the Visual Studio generator writes no compile_commands.json, LTO
        // shows in the project instead (/GL + /LTCG).
        assert_contains(&cache, "CMAKE_CXX_FLAGS_FINAL:STRING=/O2 /Ob2 /DNDEBUG");
        assert_contains(&project.read(&format!("main/build-{}-final/main.vcxproj", host())), "<WholeProgramOptimization>true</WholeProgramOptimization>");
    } else {
        assert_contains(&cache, "CMAKE_CXX_FLAGS_FINAL:STRING=-O3 -DNDEBUG");
        assert_contains(&project.read(&format!("main/build-{}-final/compile_commands.json", host())), "-flto");
    }
    assert!(project.exists(&format!("build/demo/{}/final/{}", host(), exe("demo"))));
}

#[test]
fn ci_names_the_config_in_its_directories() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--ci", "--config", "final"]).succeeds();

    assert!(project.exists(&format!("build/demo_0.1.0_{}_final/{}", host(), exe("demo"))));
}

#[test]
fn rejects_an_unknown_config() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--config", "fast"]).fails_with("unknown profile: fast");
}

// --- targets ------------------------------------------------------------------------------------

#[test]
fn ci_builds_every_declared_target_into_versioned_directories() {
    let project = Sandbox::project("demo", &[]); // declares just the host

    project.gram(&["build", "--ci"]).succeeds();

    assert!(project.exists(&format!("build/demo_0.1.0_{}_debug/{}", host(), exe("demo"))));
}

#[test]
fn ci_skips_targets_this_host_cannot_build() {
    let other = unbuildable_target();
    let project = Sandbox::project("demo", &["--target", &format!("{},{other}", host())]);

    let run = project.gram(&["build", "--ci"]).succeeds();

    assert!(run.printed(&format!("skipping module main target \"{other}\"")));
    assert!(project.exists(&format!("build/demo_0.1.0_{}_debug/{}", host(), exe("demo"))));
    assert!(!project.exists(&format!("build/demo_0.1.0_{other}_debug")));
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

// --- toolchains (toolchain.rs) ---------------------------------------------------------------------

/// A toolchain that can't build this host's own OS: msvc from linux/macos, native on Windows.
fn wrong_toolchain() -> &'static str {
    if host() == "windows" { "native" } else { "msvc" }
}

#[test]
fn the_native_toolchain_builds_like_the_default() {
    if host() == "windows" { return; }   // no native toolchain there: msvc or mingw
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["build", "--toolchain", "native"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
}

#[test]
fn refuses_a_toolchain_that_cannot_build_the_target() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--toolchain", wrong_toolchain()])
        .fails_with(&format!("{} can't build target {} from a {} host", wrong_toolchain(), host(), host()));
}

#[test]
fn rejects_an_unknown_toolchain() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--toolchain", "clang"]).fails_with("unknown toolchain: clang");
}

#[test]
fn ci_and_toolchain_cannot_be_combined() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["build", "--ci", "--toolchain", "mingw"])
        .fails_with("--toolchain can't be combined with --ci");
}

#[test]
fn warns_when_building_a_target_the_module_does_not_declare() {
    // Only a warning: the build still happens.
    let project = Sandbox::project("demo", &["--target", unbuildable_target()]);

    let run = project.gram(&["build"]).succeeds();

    assert!(run.printed(&format!("does not declare \"{}\"", host())));
    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))));
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

    let merged = project.read(&format!("main/build-{}-debug/vcpkg-manifest/vcpkg.json", host()));
    assert_contains(&merged, r#""name": "main""#);
    assert_eq!(merged.matches(r#""glm""#).count(), 1, "a port both modules use appears once:\n{merged}");
    assert_eq!(merged.matches(r#""imgui""#).count(), 1, "{merged}");
    assert_contains(&merged, r#""docking""#);
    assert_contains(&merged, r#""vulkan-binding""#);
    assert!(!project.exists(&format!("engine/build-{}-debug/vcpkg-manifest", host())),
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
