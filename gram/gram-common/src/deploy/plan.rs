//! What `gram deploy` runs for a device: a list of commands, each a program and its arguments, built here and
//! run by `deploy::execute`. Pure: no I/O, so every command line is pinned by a golden test.

use std::path::Path;

use crate::profile::Profile;

use super::device::{Device, DEVICES_FILE};

/// One command of the plan.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Step {
    /// What it does, printed before it runs ("copy the sources to legion").
    pub what: String,
    /// The program started on this machine (`rsync`, `ssh`), found on the `PATH`.
    pub program: String,
    /// Its arguments, each passed as one argument: no shell on this machine splits or expands them.
    pub args: Vec<String>,
}

impl Step {
    /// The step as it could be pasted into a shell on this machine (`--dry-run`, error messages).
    pub fn command_line(&self) -> String {
        std::iter::once(self.program.as_str())
            .chain(self.args.iter().map(String::as_str))
            .map(quote)
            .collect::<Vec<_>>()
            .join(" ")
    }
}

/// What to deploy, from the command line and the project.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeployOptions {
    /// `--config`: what `gram build` builds on the device.
    pub config: Profile,
    /// `--module`: build only this module on the device (`gram build --module`). `None`: the whole tree.
    pub module: Option<String>,
    /// The main runtime module's executable, relative to the project root (`build/gram-engine/linux/debug/Game`).
    pub executable: String,
    /// `false` with `--no-run`: copy and build only.
    pub run: bool,
    /// The project's own `gram` (gram-engine builds itself: `gram/Cargo.toml`), built first on the device and then
    /// used for `gram build` (`gram/target/debug/gram`). `None`: the device's `gram`, from its `PATH`.
    pub gram_manifest: Option<String>,
}

/// The commands deploying to `device`, in order. Written by the user.
pub fn plan(device: &Device, options: &DeployOptions) -> Vec<Step> {
    // 1. The sources, mirrored (--delete) but for what's built or this machine's own. rsync is started without a
    //    shell, so its arguments are passed as they are: quoting them would hand rsync the quotes too.
    let excluded = [".git/", "target/", "build/", "build-*/", ".vscode/settings.json", DEVICES_FILE];
    let mut args = vec!["-az".to_owned(), "--delete".to_owned()];
    for pattern in excluded {
        args.push("--exclude".to_owned());
        args.push(pattern.to_owned());
    }
    args.push("./".to_owned());
    args.push(format!("{}:{}/", device.host, device.dir));
    let mut steps = vec![Step { what: format!("copy the sources to {}", device.name), program: "rsync".to_owned(), args }];

    // 2. The project's own gram first, when it has one: the device's could be older than this tree's Gram.ron.
    let gram = match &options.gram_manifest {
        Some(manifest) => {
            steps.push(ssh(device, false, format!("build gram on {}", device.name),
                &format!("cargo build --manifest-path {}", quote(manifest))));
            // cargo's default target directory, next to the manifest: gram/Cargo.toml → gram/target/debug/gram.
            quote(&Path::new(manifest).with_file_name("target/debug/gram").to_string_lossy())
        }
        None => "gram".to_owned(),
    };

    // 3. The build.
    let mut build = format!("{gram} build --config {}", options.config);
    if let Some(module) = &options.module {
        build += &format!(" --module {}", quote(module));
    }
    steps.push(ssh(device, false, format!("build {} on {}", options.config, device.name), &build));

    // 4. The game, on the device's screen. The values are a SteamOS Desktop Mode session's (the first user, uid
    //    1000); its X authority file gets a new name at every login, so the device looks it up. pkill first: one
    //    game at a time (it fails when none runs, hence || true). exec: the game replaces the shell, so closing ssh
    //    (Ctrl+C here, through ssh -t's terminal) closes the game.
    if options.run {
        let executable = Path::new(&options.executable);
        let name = executable.file_name().map(|name| name.to_string_lossy().into_owned()).unwrap_or_default();
        let dir = executable.parent().map(|dir| dir.to_string_lossy().into_owned()).unwrap_or_default();
        let session = r#"DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 XAUTHORITY="$(ls /run/user/1000/xauth_* | head -n 1)""#;
        let run = format!("(pkill -x {} || true) && cd {} && exec env {session} {}",
            quote(&name), quote(&dir), quote(&format!("./{name}")));
        steps.push(ssh(device, true, format!("run {} on {}", options.executable, device.name), &run));
    }

    steps
}

/// A step running `command` on the device, inside the project's directory.
fn ssh(device: &Device, terminal: bool, what: String, command: &str) -> Step {
    let mut args = Vec::new();
    if terminal {
        args.push("-t".to_owned()); // a terminal on the device: Ctrl+C reaches the game, not just ssh
    }
    args.push(device.host.clone());
    args.push(remote(device, command));
    Step { what, program: "ssh".to_owned(), args }
}

/// `command` wrapped in the layers between ssh and the project: the device's login shell runs this text, `bash -lc`
/// runs the quoted script (inside the distrobox, when there's one), the script sources the environment and goes to
/// the project. Each layer reads one level of quotes away, so each inner layer is quoted once, here.
fn remote(device: &Device, command: &str) -> String {
    let mut script = String::new();
    if let Some(env) = &device.env {
        script += &format!("source {env} && "); // as written: ~ must stay expandable by the device's shell
    }
    script += &format!("cd ~/{} && {command}", quote(&device.dir));

    let shell = format!("bash -lc {}", quote(&script));
    match &device.container {
        Some(container) => format!("distrobox enter {} -- {shell}", quote(container)),
        None => shell,
    }
}

/// `arg` quoted for a POSIX shell, so the shell reads it back as exactly one word, unchanged. Written by the user.
pub fn quote(arg: &str) -> String {
    // Letters, digits and these mean nothing to a shell: left bare, so printed commands stay readable.
    let safe = |c: char| c.is_ascii_alphanumeric() || "_-./:=@,+%".contains(c);
    if !arg.is_empty() && arg.chars().all(safe) {
        return arg.to_owned();
    }

    // Inside '...' a shell takes everything literally, but a ' can't appear there: close the quotes, add an
    // escaped \', open them again. it's → 'it'\''s'.
    format!("'{}'", arg.replace('\'', r"'\''"))
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn quote_string_arguments() {
        assert_eq!(quote("legion"), "legion");
        assert_eq!(quote("build-*/"), "'build-*/'");
        assert_eq!(quote("a b"), "'a b'");
        assert_eq!(quote("~/.gram-env"), "'~/.gram-env'");
        assert_eq!(quote("it's"), r"'it'\''s'"); // raw: in a plain Rust string \' is just '
        assert_eq!(quote(""), "''");
    }

    fn legion() -> Device {
        Device {
            name: "legion".into(),
            host: "legion".into(),
            dir: "Projects/gram-engine".into(),
            container: Some("gram".into()),
            env: Some("~/.gram-env".into()),
        }
    }

    fn gram_engine() -> DeployOptions {
        DeployOptions {
            config: Profile::Debug,
            module: None,
            executable: "build/gram-engine/linux/debug/Game".into(),
            run: true,
            gram_manifest: Some("gram/Cargo.toml".into()),
        }
    }

    fn args(step: &Step) -> Vec<&str> {
        step.args.iter().map(String::as_str).collect()
    }

    #[test]
    fn gram_engine_on_the_legion_go() {
        let steps = plan(&legion(), &gram_engine());
        assert_eq!(steps.len(), 4);

        assert_eq!(steps[0].what, "copy the sources to legion");
        assert_eq!(steps[0].program, "rsync");
        assert_eq!(args(&steps[0]), [
            "-az", "--delete",
            "--exclude", ".git/", "--exclude", "target/", "--exclude", "build/", "--exclude", "build-*/",
            "--exclude", ".vscode/settings.json", "--exclude", "Devices.ron",
            "./", "legion:Projects/gram-engine/",
        ]);

        assert_eq!(steps[1].what, "build gram on legion");
        assert_eq!(steps[1].program, "ssh");
        assert_eq!(args(&steps[1]), [
            "legion",
            "distrobox enter gram -- bash -lc 'source ~/.gram-env && cd ~/Projects/gram-engine && cargo build --manifest-path gram/Cargo.toml'",
        ]);

        assert_eq!(steps[2].what, "build debug on legion");
        assert_eq!(args(&steps[2]), [
            "legion",
            "distrobox enter gram -- bash -lc 'source ~/.gram-env && cd ~/Projects/gram-engine && gram/target/debug/gram build --config debug'",
        ]);

        assert_eq!(steps[3].what, "run build/gram-engine/linux/debug/Game on legion");
        assert_eq!(args(&steps[3]), [
            "-t",
            "legion",
            r#"distrobox enter gram -- bash -lc 'source ~/.gram-env && cd ~/Projects/gram-engine && (pkill -x Game || true) && cd build/gram-engine/linux/debug && exec env DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 XAUTHORITY="$(ls /run/user/1000/xauth_* | head -n 1)" ./Game'"#,
        ]);
    }

    #[test]
    fn a_plain_linux_box_without_container_env_or_run() {
        let device = Device { name: "box".into(), host: "me@10.0.0.2".into(), dir: "gram-engine".into(), container: None, env: None };
        let options = DeployOptions { config: Profile::Release, module: Some("Game".into()), run: false, gram_manifest: None, ..gram_engine() };

        let steps = plan(&device, &options);

        assert_eq!(steps.len(), 2); // no gram of its own to build, nothing to run
        assert_eq!(args(&steps[0]).last(), Some(&"me@10.0.0.2:gram-engine/"));
        assert_eq!(steps[1].what, "build release on box");
        assert_eq!(args(&steps[1]), ["me@10.0.0.2", "bash -lc 'cd ~/gram-engine && gram build --config release --module Game'"]);
    }

    /// A space in the device's folder: quoted once for the script, and that quoting quoted again for bash -lc.
    #[test]
    fn a_folder_with_a_space_is_quoted_at_both_layers() {
        let device = Device { dir: "My Games/gram".into(), container: None, env: None, ..legion() };
        let options = DeployOptions { run: false, gram_manifest: None, ..gram_engine() };

        let steps = plan(&device, &options);

        assert_eq!(args(&steps[0]).last(), Some(&"legion:My Games/gram/")); // rsync: no shell, no quotes
        assert_eq!(args(&steps[1])[1], r"bash -lc 'cd ~/'\''My Games/gram'\'' && gram build --config debug'");
    }

    #[test]
    fn a_command_line_is_ready_to_paste() {
        let steps = plan(&legion(), &gram_engine());
        assert_eq!(
            steps[2].command_line(),
            r"ssh legion 'distrobox enter gram -- bash -lc '\''source ~/.gram-env && cd ~/Projects/gram-engine && gram/target/debug/gram build --config debug'\'''"
        );
    }
}