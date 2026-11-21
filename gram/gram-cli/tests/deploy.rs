//! `gram deploy [--device <name>] [--config ...] [--module <name>] [--no-run] [--dry-run]`: copies the project to a
//! device listed in `Devices.ron` (next to `Gram.ron`, git-ignored), builds it there and starts the game. These
//! tests never reach a device: they check the manifest's errors, and the plan through `--dry-run`, which only
//! prints the commands.

mod support;
use support::*;

const LEGION: &str = r#"Devices(
    default: Some("legion"),
    devices: [
        Device(name: "legion", host: "legion", dir: "Projects/demo", container: Some("gram"), env: Some("~/.gram-env")),
    ],
)
"#;

#[test]
fn without_a_devices_file_says_what_to_write() {
    let project = Sandbox::project("demo", &[]);

    project.gram(&["deploy", "--dry-run"]).fails_with("no Devices.ron next to Gram.ron");
}

#[test]
fn an_unknown_device_names_the_known_ones() {
    let project = Sandbox::project("demo", &[]);
    project.write("Devices.ron", LEGION);

    project.gram(&["deploy", "--dry-run", "--device", "deck"]).fails_with("no device named deck in Devices.ron (known: legion)");
}

#[test]
fn a_malformed_devices_file_points_at_it() {
    let project = Sandbox::project("demo", &[]);
    project.write("Devices.ron", "Devices(devices: [Device(name: \"legion\")])");

    project.gram(&["deploy", "--dry-run"]).fails_with("Devices.ron");
}

/// The whole plan for the manifest's default device, as `--dry-run` prints it: the commands of the spec,
/// as a user would paste them.
#[test]
fn dry_run_prints_the_plan_for_the_default_device() {
    let project = Sandbox::project("demo", &[]);
    project.write("Devices.ron", LEGION);

    let run = project.gram(&["deploy", "--dry-run"]).succeeds();

    // demo has no gram/Cargo.toml: the device's own gram builds it, so no "build gram" step.
    assert_contains(&run.stdout, "gram deploy [1/4]: copy the sources to legion");
    assert_contains(&run.stdout, "  rsync -az --delete --exclude .git/ --exclude target/ --exclude build/ --exclude 'build-*/' --exclude .vscode/settings.json --exclude Devices.ron ./ legion:Projects/demo/");
    assert_contains(&run.stdout, "gram deploy [2/4]: build debug on legion");
    assert_contains(&run.stdout, r#"  ssh legion 'distrobox enter gram -- bash -lc '\''source ~/.gram-env && cd ~/Projects/demo && gram build --config debug'\'''"#);
    assert_contains(&run.stdout, "gram deploy [3/4]: install the Steam launcher on legion");
    assert_contains(&run.stdout, "gram deploy [4/4]: run build/demo/linux/debug/demo on legion");
}
