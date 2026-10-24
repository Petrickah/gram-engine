//! `gram add [<dependency>] --module <name>`: creates `--module` if it doesn't exist yet, then
//! (if a dependency name is given) adds that dependency to it. A dependency comes from one of:
//!
//! - the language's registry (cargo: crates.io) — plain `gram add <name>`;
//! - a git repository via CMake FetchContent — `--url` (+ optional `--tag`), cmake only;
//! - a vcpkg port — `--vcpkg`, cmake only;
//! - another module of the same project — `--module-ref`.

mod support;
use support::*;

// --- creating modules -------------------------------------------------------------------------

#[test]
fn creates_the_module_when_it_does_not_exist() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();

    let config = project.config();
    let engine = config.module("engine");
    assert_eq!(engine.lang, "cmake");
    assert_eq!(engine.kind, "library");
    assert_eq!(engine.src, "engine/CMakeLists.txt");
    assert!(engine.deps.is_empty());
    assert_contains(&project.read("engine/CMakeLists.txt"), "add_library(engine SHARED src/engine.cpp)");
    assert_eq!(config.main, "main", "adding a module doesn't change the default one");
}

#[test]
fn adding_a_cargo_module_updates_rust_analyzer_settings() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "--module", "tool", "--lang", "cargo"]).succeeds();

    assert!(project.exists("tool/Cargo.toml"));
    assert_contains(&project.read(".vscode/settings.json"), r#""tool/Cargo.toml""#);
}

#[test]
fn rejects_an_unknown_language() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["add", "--module", "bad", "--lang", "zig"])
        .fails_with("unsupported module language: zig");
    assert!(!project.config().has_module("bad"));
}

// --- targets ------------------------------------------------------------------------------------

#[test]
fn adds_targets_to_an_existing_module() {
    let project = Sandbox::project("demo", &["--target", "linux"]);

    let run = project.gram(&["add", "--module", "main", "--target", "windows,macos"]).succeeds();

    assert!(run.printed("added target(s) windows, macos"));
    assert_eq!(project.config().module("main").targets, ["linux", "windows", "macos"]);
}

#[test]
fn does_not_duplicate_a_target_already_declared() {
    let project = Sandbox::project("demo", &["--target", "linux,windows"]);

    let run = project.gram(&["add", "--module", "main", "--target", "linux"]).succeeds();

    assert!(!run.printed("added target(s)"));
    assert_eq!(project.config().module("main").targets, ["linux", "windows"]);
}

#[test]
fn rejects_an_unknown_target() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["add", "--module", "main", "--target", "playstation"])
        .fails_with("unsupported target: playstation");
}

// --- FetchContent (cmake + --url) ---------------------------------------------------------------

#[test]
fn adds_a_fetchcontent_dependency_to_a_cmake_module() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "fmt", "--module", "main",
                   "--url", "https://github.com/fmtlib/fmt", "--tag", "11.0.2"]).succeeds();

    // The dependency goes inside the managed region, between its own begin/end markers, so
    // `gram remove` can find and cut exactly this block later.
    let cmakelists = project.read("main/CMakeLists.txt");
    assert_contains(&cmakelists, "# gram:dep:fmt begin\nFetchContent_Declare(\n  fmt\n  GIT_REPOSITORY https://github.com/fmtlib/fmt\n  GIT_TAG 11.0.2\n)\nFetchContent_MakeAvailable(fmt)\n# gram:dep:fmt end\n# --- end gram dependencies ---");
    assert_eq!(project.config().module("main").deps, ["fmt"]);
}

#[test]
fn the_tag_is_optional() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "fmt", "--module", "main", "--url", "https://github.com/fmtlib/fmt"]).succeeds();

    let cmakelists = project.read("main/CMakeLists.txt");
    assert_contains(&cmakelists, "GIT_REPOSITORY https://github.com/fmtlib/fmt\n)");
    assert_not_contains(&cmakelists, "GIT_TAG");
}

#[test]
fn adding_a_dependency_the_module_already_has_only_warns() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "fmt", "--module", "main", "--url", "https://github.com/fmtlib/fmt"]).succeeds();
    let before = project.read("main/CMakeLists.txt");

    let run = project.gram(&["add", "fmt", "--module", "main", "--url", "https://github.com/fmtlib/fmt"]).succeeds();

    assert!(run.printed("warning: module main already depends on fmt, nothing to do"));
    assert_eq!(project.read("main/CMakeLists.txt"), before, "nothing is written again");
    assert_eq!(project.config().module("main").deps, ["fmt"], "recorded once in Gram.ron");
}

#[test]
fn the_duplicate_check_is_by_name_whatever_the_source() {
    // Same name, different source: still the same dependency as far as the module goes.
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "glm", "--module", "main", "--vcpkg"]).succeeds();

    let run = project.gram(&["add", "glm", "--module", "main", "--url", "https://github.com/g-truc/glm"]).succeeds();

    assert!(run.printed("already depends on glm"));
    assert_not_contains(&project.read("main/CMakeLists.txt"), "FetchContent_Declare(\n  glm");
}

#[test]
fn a_cmake_dependency_needs_a_url_or_vcpkg() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "fmt", "--module", "main"])
        .fails_with("cmake dependencies need --url");
}

// --- vcpkg (cmake + --vcpkg) --------------------------------------------------------------------

#[test]
fn adds_a_vcpkg_dependency_to_a_cmake_module() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "glm", "--module", "main", "--vcpkg"]).succeeds();

    // Recorded in the module's vcpkg manifest (created on first use)...
    let manifest = project.read("main/vcpkg.json");
    assert_contains(&manifest, r#""name": "main""#);
    assert_contains(&manifest, r#""glm""#);
    // ...and found by CMake. Linking it (target_link_libraries) is left to you, since the
    // target name varies per vcpkg port.
    assert_contains(&project.read("main/CMakeLists.txt"),
        "# gram:dep:glm begin\nfind_package(glm CONFIG REQUIRED)\n# gram:dep:glm end");
    assert_eq!(project.config().module("main").deps, ["glm"]);
}

#[test]
fn adds_more_vcpkg_dependencies_to_the_same_manifest() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "glm", "--module", "main", "--vcpkg"]).succeeds();
    project.gram(&["add", "glfw3", "--module", "main", "--vcpkg"]).succeeds();

    let manifest = project.read("main/vcpkg.json");
    assert_contains(&manifest, r#""glm""#);
    assert_contains(&manifest, r#""glfw3""#);
}

#[test]
fn vcpkg_cannot_be_combined_with_url_or_tag() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "sdl2", "--module", "main", "--vcpkg", "--url", "https://example.com/sdl"])
        .fails_with("--vcpkg can't be combined with --url/--tag");
    project.gram(&["add", "sdl2", "--module", "main", "--vcpkg", "--tag", "v2"])
        .fails_with("--vcpkg can't be combined with --url/--tag");
}

// --- cargo registry dependencies ----------------------------------------------------------------

#[test]
#[ignore = "needs network access to crates.io; run with `cargo test -- --ignored`"]
fn adds_a_crates_io_dependency_to_a_cargo_module() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["add", "serde", "--module", "main"]).succeeds();

    assert_contains(&project.read("main/Cargo.toml"), "serde");
    assert_eq!(project.config().module("main").deps, ["serde"]);
}

#[test]
fn a_cargo_module_rejects_cmake_only_sources() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["add", "serde", "--module", "main", "--url", "https://github.com/serde-rs/serde"])
        .fails_with("cargo modules take plain registry dependencies");
    project.gram(&["add", "serde", "--module", "main", "--vcpkg"])
        .fails_with("cargo modules take plain registry dependencies");
}

// --- module references (--module-ref) -----------------------------------------------------------

#[test]
fn links_a_cmake_module_to_a_cmake_library() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();

    project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();

    let cmakelists = project.read("main/CMakeLists.txt");
    // The library's directory is built as part of main's build...
    assert_contains(&cmakelists,
        "# gram:dep:engine begin\nadd_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)\n# gram:dep:engine end");
    // ...and linked. Unlike an external dependency, gram knows the target name: it made it.
    // This block goes after add_executable, since the target must exist first.
    assert_contains(&cmakelists,
        "add_executable(main src/main.cpp)\n\n# gram:link:engine begin\ntarget_link_libraries(main PRIVATE engine)\n# gram:link:engine end");
    assert_eq!(project.config().module("main").deps, ["engine"]);
}

#[test]
fn links_a_cargo_module_to_a_cargo_library() {
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "util", "--lang", "cargo", "--kind", "library"]).succeeds();

    project.gram(&["add", "util", "--module", "main", "--module-ref"]).succeeds();

    // A path dependency: no registry involved.
    assert_contains(&project.read("main/Cargo.toml"), r#"util = { version = "0.1.0", path = "../util" }"#);
    assert_eq!(project.config().module("main").deps, ["util"]);
}

#[test]
fn referencing_the_same_module_twice_only_warns() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();
    project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();

    let run = project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();

    assert!(run.printed("already depends on engine"));
    assert_eq!(project.read("main/CMakeLists.txt").matches("target_link_libraries(main PRIVATE engine)").count(), 1);
    assert_eq!(project.config().module("main").deps, ["engine"]);
}

#[test]
fn a_module_ref_must_point_at_an_existing_module() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "nowhere", "--module", "main", "--module-ref"])
        .fails_with("referenced module not found: nowhere");
}

#[test]
fn a_module_ref_must_point_at_a_library() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "tool", "--lang", "cmake"]).succeeds(); // a runtime module

    project.gram(&["add", "tool", "--module", "main", "--module-ref"])
        .fails_with("only library modules can be referenced");
}

#[test]
fn a_module_cannot_reference_itself() {
    let project = Sandbox::project("demo", &["--lang", "cmake", "--kind", "library"]);

    project.gram(&["add", "main", "--module", "main", "--module-ref"])
        .fails_with("a module can't reference itself");
}

#[test]
fn module_refs_cannot_cross_languages() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "util", "--lang", "cargo", "--kind", "library"]).succeeds();

    project.gram(&["add", "util", "--module", "main", "--module-ref"])
        .fails_with("cross-language module references aren't supported yet");
}

#[test]
fn a_module_ref_cannot_have_a_url_or_vcpkg() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();

    project.gram(&["add", "engine", "--module", "main", "--module-ref", "--vcpkg"])
        .fails_with("--module-ref can't be combined with --url/--tag/--vcpkg");
}

// --- --path ---------------------------------------------------------------------------------

#[test]
fn path_runs_the_command_in_another_directory() {
    // Equivalent to `cd demo && gram add ...`, without the cd.
    let sandbox = Sandbox::empty();
    sandbox.gram(&["init", "demo", "--lang", "cmake"]).succeeds();

    sandbox.gram(&["add", "--path", "demo", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();

    assert!(sandbox.exists("demo/engine/CMakeLists.txt"));
}
