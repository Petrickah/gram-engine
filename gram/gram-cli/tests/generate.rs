//! `gram generate`: finds the component schemas (`.ron` files starting with `Component(`) of each
//! cargo module and writes `components.generated.rs` + `Components.generated.hpp` next to its
//! `Cargo.toml`. `gram build` runs it first.

mod support;
use support::*;

const RUST: &str = "main/components.generated.rs";
const CPP: &str = "main/Components.generated.hpp";

#[test]
fn a_module_without_schemas_generates_nothing() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["generate"]).succeeds();

    assert!(!project.exists(RUST));
    assert!(!project.exists(CPP));
}

#[test]
fn only_component_schemas_are_picked_up() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/scripts/greeting.ron", "// A component.\nComponent(\n    name: \"Greeting\",\n    fields: [],\n)\n");
    project.write("main/settings.ron", "Settings(volume: 3)\n");                 // other data: ignored
    project.write("main/target/stale.ron", "Component(name: \"Stale\", fields: [])\n"); // build output: ignored

    assert!(project.gram(&["generate"]).succeeds().printed("1 schema(s)"));

    let rust = project.read(RUST);
    assert!(rust.contains("pub struct Greeting {"), "{rust}");
    assert!(!rust.contains("Stale"), "{rust}");
    assert!(project.read(CPP).contains("struct Greeting {"));
}

#[test]
fn fields_are_laid_out_like_c() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/motion.ron", "Component(name: \"Motion\", fields: [\n    Field(name: \"enabled\", ty: Bool),\n    Field(name: \"max_speed\", ty: F32),\n])\n");

    project.gram(&["generate"]).succeeds();

    // A bool, 3 bytes of padding up to the f32's alignment, the f32.
    let rust = project.read(RUST);
    assert!(rust.contains("pub max_speed: f32,"), "{rust}");
    assert!(rust.contains("size_of::<Motion>() == 8"), "{rust}");
    assert!(rust.contains("offset_of!(Motion, max_speed) == 4"), "{rust}");

    // C++ names the same fields in PascalCase.
    let cpp = project.read(CPP);
    assert!(cpp.contains("float MaxSpeed;"), "{cpp}");
    assert!(cpp.contains("static_assert(offsetof(Motion, MaxSpeed) == 4);"), "{cpp}");
    assert!(cpp.contains("static_assert(sizeof(Motion) == 8);"), "{cpp}");
}

#[test]
fn an_unchanged_schema_rewrites_nothing() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/greeting.ron", "Component(name: \"Greeting\", fields: [Field(name: \"text\", ty: Str(64))])\n");
    project.gram(&["generate"]).succeeds();

    assert!(project.gram(&["generate"]).succeeds().printed("generated code unchanged"));
}

#[test]
fn a_broken_schema_names_its_file() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/broken.ron", "Component(name: \"Broken\", fields: [Field(name: \"x\", ty: Str(\"x\"))])\n");

    project.gram(&["generate"]).fails_with("broken.ron");
}

#[test]
fn rejects_a_module_that_is_not_cargo() {
    let project = Sandbox::project("demo", &["--lang", "cmake"]);

    project.gram(&["generate", "--module", "main"])
        .fails_with("component code is generated for cargo modules");
}

#[test]
fn build_generates_the_component_code_first() {
    let project = Sandbox::project("demo", &[]);
    project.write("main/greeting.ron", "Component(name: \"Greeting\", fields: [Field(name: \"text\", ty: Str(64))])\n");

    project.gram(&["build"]).succeeds();

    assert!(project.exists(RUST));
    assert!(project.exists(CPP));
}
