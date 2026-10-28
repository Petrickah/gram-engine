//! `gram run [--module <name>] [-- <args>...]`: runs an already-built runtime module's binary
//! from `build/<project>/<target>/`. It never builds first; anything after `--` goes to the
//! binary, and the binary's exit code becomes gram's.

mod support;
use support::*;

#[test]
fn runs_the_main_module_by_default() {
    let project = Sandbox::project("demo", &[]);
    project.gram(&["build"]).succeeds();

    let run = project.gram(&["run"]).succeeds();

    assert_contains(&run.stdout, "Hello, world!"); // cargo init's main.rs
}

#[test]
fn runs_a_cmake_executable() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);
    project.gram(&["build"]).succeeds();

    let run = project.gram(&["run"]).succeeds();

    assert_contains(&run.stdout, "Hello, World!"); // gram's own C++ scaffold
}

#[test]
fn runs_another_module_with_module_flag() {
    let project = Sandbox::project("demo", &[]);
    project.gram(&["add", "--module", "tool", "--lang", "cargo"]).succeeds();
    project.write("tool/src/main.rs", "fn main() { println!(\"this is tool\"); }\n");
    project.gram(&["build"]).succeeds();

    let run = project.gram(&["run", "--module", "tool"]).succeeds();

    assert_contains(&run.stdout, "this is tool");
}

#[test]
fn forwards_arguments_after_double_dash() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/src/main.rs",
        "fn main() { println!(\"args: {:?}\", std::env::args().skip(1).collect::<Vec<_>>()); }\n");
    project.gram(&["build"]).succeeds();

    let run = project.gram(&["run", "--", "--level", "1"]).succeeds();

    assert_contains(&run.stdout, r#"args: ["--level", "1"]"#);
}

#[test]
fn passes_the_exit_code_through() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/src/main.rs", "fn main() { std::process::exit(3); }\n");
    project.gram(&["build"]).succeeds();

    let run = project.gram(&["run"]);

    assert_eq!(run.code, Some(3));
}

#[test]
fn does_not_build_first() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["run"]).fails_with("build artifact not found");
}

#[test]
fn refuses_to_run_a_library() {
    let project = Sandbox::project("demo", &["--lang", "cmake", "--kind", "library"]);

    project.gram(&["run"]).fails_with("not runnable (only \"runtime\" modules are)");
}

#[test]
fn needs_a_module_when_the_project_has_no_default() {
    let project = Sandbox::project("demo", &[]);
    project.gram(&["remove", "--module", "main"]).succeeds();

    project.gram(&["run"]).fails_with("no --module given");
}

#[test]
fn rejects_an_unknown_module() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["run", "--module", "nowhere"]).fails_with("module not found: nowhere");
}
