//! Nested Gram projects: a module with `--lang gram` is another Gram project (its `src` is that
//! project's Gram.ron). Its modules are built, referenced and edited like the root's own, but
//! stay declared in its file, with `src` relative to it.

mod support;
use support::*;

/// A cmake project with a nested gram project `game` in it.
fn project_with_game() -> Sandbox {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["add", "--module", "game", "--lang", "gram"]).succeeds();
    project
}

// --- creating ------------------------------------------------------------------------------------

#[test]
fn a_gram_module_is_its_own_gram_ron() {
    let project = project_with_game();

    let game = project.config().module("game").src.clone();
    assert_eq!(game, "game/Gram.ron");
    let nested = project.config_at("game");
    assert_eq!(nested.name, "game");
    assert!(nested.modules.is_empty());
}

#[test]
fn parent_creates_a_module_inside_the_nested_project() {
    let project = project_with_game();

    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "game"]).succeeds();

    assert!(project.exists("game/app/CMakeLists.txt"));
    // Declared in the nested Gram.ron, relative to it — not in the root one.
    assert_eq!(project.config_at("game").module("app").src, "app/CMakeLists.txt");
    assert!(!project.config().has_module("app"));
}

#[test]
fn parent_must_be_a_gram_module() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "main"])
        .fails_with("not a gram module");
    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "nowhere"])
        .fails_with("--parent module not found: nowhere");
}

#[test]
fn a_gram_module_takes_no_dependencies() {
    let project = project_with_game();

    project.gram(&["add", "glm", "--module", "game", "--vcpkg"])
        .fails_with("a gram module is a collection of modules");
}

// --- references across projects ------------------------------------------------------------------

#[test]
fn a_nested_module_references_a_module_of_the_parent_project() {
    let project = project_with_game();
    project.gram(&["add", "--module", "engine", "--lang", "cmake", "--kind", "library"]).succeeds();
    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "game"]).succeeds();

    project.gram(&["add", "engine", "--module", "app", "--module-ref"]).succeeds();

    assert_contains(&project.read("game/app/CMakeLists.txt"),
        "add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)");
    assert_eq!(project.config_at("game").module("app").deps, [module_ref("engine")]);
    // The root Gram.ron is written back unchanged in shape: src still relative to it.
    assert_eq!(project.config().module("engine").src, "engine/CMakeLists.txt");
}

#[test]
fn a_nested_cargo_module_references_a_cargo_library_of_the_parent() {
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "game", "--lang", "gram"]).succeeds();
    project.gram(&["add", "--module", "util", "--lang", "cargo", "--kind", "library"]).succeeds();
    project.gram(&["add", "--module", "logic", "--lang", "cargo", "--kind", "library", "--parent", "game"]).succeeds();

    project.gram(&["add", "util", "--module", "logic", "--module-ref"]).succeeds();

    assert_contains(&project.read("game/logic/Cargo.toml"), r#"path = "../../util""#);
}

#[test]
fn module_names_are_unique_across_nested_projects() {
    let project = project_with_game();
    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "game"]).succeeds();
    // Hand-edited: the root declares a module with the same name as one in game/.
    project.edit("Gram.ron", r#"name: "main""#, r#"name: "app""#);

    project.gram(&["build"]).fails_with("module name app is used twice");
}

// --- building and removing -----------------------------------------------------------------------

#[test]
fn builds_a_nested_module_into_the_root_build_directory() {
    let project = project_with_game();
    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "game"]).succeeds();

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("app"))));
    assert!(project.exists(&format!("game/app/build-{}-debug/CMakeCache.txt", host())));
}

#[test]
fn building_a_gram_module_builds_everything_inside_it() {
    let project = project_with_game();
    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "game"]).succeeds();

    project.gram(&["build", "--module", "game"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("app"))));
    assert!(!project.exists(&format!("build/demo/{}/debug/{}", host(), exe("demo"))), "main is outside game");
}

#[test]
fn a_nested_projects_main_module_is_named_after_the_nested_project() {
    let project = project_with_game();
    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "game"]).succeeds();
    project.edit("game/Gram.ron", r#"main: """#, r#"main: "app""#);

    project.gram(&["build"]).succeeds();

    assert!(project.exists(&format!("build/demo/{}/debug/{}", host(), exe("game"))));
    assert!(!project.exists(&format!("build/demo/{}/debug/{}", host(), exe("app"))));
}

#[test]
fn a_gram_module_cannot_be_run() {
    let project = project_with_game();

    project.gram(&["run", "--module", "game"]).fails_with("is a gram module");
}

#[test]
fn removing_a_gram_module_removes_its_modules_too() {
    let project = project_with_game();
    project.gram(&["add", "--module", "app", "--lang", "cmake", "--parent", "game"]).succeeds();

    project.gram(&["remove", "--module", "game", "--purge"]).succeeds();

    assert!(!project.config().has_module("game"));
    assert!(!project.exists("game"), "--purge deletes the nested project, and nothing writes it back");
    project.gram(&["build"]).succeeds();
}
