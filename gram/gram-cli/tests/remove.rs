//! `gram remove [<dependency>] --module <name>`: with a dependency name, takes that dependency
//! out of the module (undoing exactly what `gram add` wrote); without one, removes the whole
//! module from Gram.ron (and with `--purge`, its directory too).

mod support;
use support::*;

// --- removing dependencies ----------------------------------------------------------------------

#[test]
fn removes_a_fetchcontent_dependency() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    let before = project.read("main/CMakeLists.txt");
    project.gram(&["add", "fmt", "--module", "main", "--url", "https://github.com/fmtlib/fmt"]).succeeds();

    project.gram(&["remove", "fmt", "--module", "main"]).succeeds();

    assert_eq!(project.read("main/CMakeLists.txt"), before, "the CMakeLists is back to how it was");
    assert!(project.config().module("main").deps.is_empty());
}

#[test]
fn removes_a_vcpkg_dependency_from_cmake_and_the_manifest() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "glm", "--module", "main", "--vcpkg"]).succeeds();
    project.gram(&["add", "glfw3", "--module", "main", "--vcpkg"]).succeeds();

    project.gram(&["remove", "glm", "--module", "main"]).succeeds();

    assert_not_contains(&project.read("main/CMakeLists.txt"), "find_package(glm");
    let manifest = project.read("main/vcpkg.json");
    assert_not_contains(&manifest, r#""glm""#);
    assert_contains(&manifest, r#""glfw3""#);
    assert_eq!(project.config().module("main").deps, [vcpkg("glfw3")]);
}

#[test]
fn removes_a_cmake_module_reference_and_its_link() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();
    let before = project.read("main/CMakeLists.txt");
    project.gram(&["add", "engine", "--module", "main", "--module-ref"]).succeeds();

    project.gram(&["remove", "engine", "--module", "main"]).succeeds();

    // Both blocks gram wrote are gone: add_subdirectory and target_link_libraries.
    assert_eq!(project.read("main/CMakeLists.txt"), before);
    assert!(project.config().has_module("engine"), "only the reference is removed, not the library module");
}

#[test]
fn removes_a_cargo_module_reference() {
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "util", "--lang", "cargo", "--kind", "library"]).succeeds();
    project.gram(&["add", "util", "--module", "main", "--module-ref"]).succeeds();

    project.gram(&["remove", "util", "--module", "main"]).succeeds();

    assert_not_contains(&project.read("main/Cargo.toml"), "util");
    assert!(project.config().module("main").deps.is_empty());
}

#[test]
fn removing_a_dependency_that_is_not_there_fails() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["remove", "fmt", "--module", "main"])
        .fails_with("dependency not found in CMakeLists.txt");
}

#[test]
fn removing_a_dependency_from_a_missing_module_fails() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    let run = project.gram(&["remove", "fmt", "--module", "nowhere"])
        .fails_with("module not found: nowhere");

    assert!(!run.printed("panicked"), "a normal error, not a crash");
}

// --- removing whole modules ---------------------------------------------------------------------

#[test]
fn removes_a_module_but_keeps_its_files() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();

    project.gram(&["remove", "--module", "engine"]).succeeds();

    assert!(!project.config().has_module("engine"));
    assert!(project.exists("engine/CMakeLists.txt"), "without --purge, the files stay on disk");
}

#[test]
fn purge_also_deletes_the_module_directory() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();

    project.gram(&["remove", "--module", "engine", "--purge"]).succeeds();

    assert!(!project.config().has_module("engine"));
    assert!(!project.exists("engine"));
}

#[test]
fn removing_a_missing_module_fails() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["remove", "--module", "nowhere"])
        .fails_with("module not found: nowhere");
}

#[test]
fn removing_the_main_module_clears_the_default() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["remove", "--module", "main"]).succeeds();

    assert_eq!(project.config().main, "", "`gram run` no longer has a module to fall back to");
}

#[test]
fn removing_a_cargo_module_updates_rust_analyzer_settings() {
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "tool", "--lang", "cargo"]).succeeds();

    project.gram(&["remove", "--module", "tool"]).succeeds();

    let settings = project.read(".vscode/settings.json");
    assert_not_contains(&settings, "tool/Cargo.toml");
    assert_contains(&settings, "main/Cargo.toml");
}
