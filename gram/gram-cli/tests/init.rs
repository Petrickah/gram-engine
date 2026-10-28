//! `gram init <name>`: creates a project directory with a Gram.ron and one module, `main`,
//! which also becomes the project's default module for `gram run`.

mod support;
use support::*;

#[test]
fn creates_a_cargo_project_by_default() {
    let project = Sandbox::project("demo", &[]);

    let config = project.config();
    assert_eq!(config.name, "demo");
    assert_eq!(config.version, "0.1.0");
    assert_eq!(config.main, "main", "the first module becomes the default for `gram run`");

    let main = config.module("main");
    assert_eq!(main.lang, "cargo");
    assert_eq!(main.kind, "runtime");
    assert_eq!(main.src, "main/Cargo.toml");
    assert_eq!(main.targets, [host()], "without --target, a module targets just the host OS");

    // `cargo init` did the scaffolding.
    assert!(project.exists("main/Cargo.toml"));
    assert!(project.exists("main/src/main.rs"));
}

#[test]
fn points_rust_analyzer_at_cargo_modules() {
    // The project root isn't a Cargo project itself, so rust-analyzer needs to be told where
    // the crates are.
    let project = Sandbox::project("demo", &[]);

    assert_contains(&project.read(".vscode/settings.json"), r#""rust-analyzer.linkedProjects""#);
    assert_contains(&project.read(".vscode/settings.json"), r#""main/Cargo.toml""#);
}

#[test]
fn creates_a_cmake_executable_project() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    let config = project.config();
    let main = config.module("main");
    assert_eq!(main.lang, "cmake");
    assert_eq!(main.kind, "runtime");
    assert_eq!(main.src, "main/CMakeLists.txt");

    let cmakelists = project.read("main/CMakeLists.txt");
    assert_contains(&cmakelists, "add_executable(main src/main.cpp)");
    // The region `gram add` / `gram remove` edit later.
    assert_contains(&cmakelists, "# --- gram dependencies (managed, do not edit by hand) ---");
    assert_contains(&cmakelists, "# --- end gram dependencies ---");
    assert!(project.exists("main/src/main.cpp"));

    assert!(!project.exists(".vscode/settings.json"), "no cargo module, nothing to tell rust-analyzer");
}

#[test]
fn creates_a_cmake_shared_library_project() {
    let project = Sandbox::project("demo", &["--lang", "cmake", "--kind", "library"]);

    assert_eq!(project.config().module("main").kind, "library");
    assert_contains(&project.read("main/CMakeLists.txt"), "add_library(main SHARED src/main.cpp)");
    assert!(project.exists("main/include/main/main.hpp"), "a library gets a public header");
    assert!(project.exists("main/src/main.cpp"));
}

#[test]
fn records_the_requested_targets() {
    let project = Sandbox::project("demo", &["--target", "linux,windows"]);

    assert_eq!(project.config().module("main").targets, ["linux", "windows"]);
}

#[test]
fn rejects_an_unknown_target() {
    Sandbox::empty()
        .gram(&["init", "demo", "--target", "playstation"])
        .fails_with("unsupported target: playstation");
}

#[test]
fn rejects_an_unknown_language() {
    Sandbox::empty()
        .gram(&["init", "demo", "--lang", "zig"])
        .fails_with("unsupported module language: zig");
}

#[test]
fn git_flag_initializes_a_repository() {
    let project = Sandbox::project("demo", &["--git"]);

    assert!(project.exists(".git"));
}
