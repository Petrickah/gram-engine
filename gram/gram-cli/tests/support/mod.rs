//! Shared helpers for gram's command tests.
//!
//! Every test gets its own empty directory (a *sandbox*) and runs the real `gram` binary in it,
//! exactly like you would from a terminal. The tests only look at what a user can see: the
//! files gram writes and what it prints. That keeps them valid however gram is restructured
//! inside, which is the point of having them.
//!
//! When a test fails, its sandbox is kept and its path printed, so you can `cd` into it and
//! look at what gram actually produced.

#![allow(dead_code)] // each test file uses a different subset of these helpers

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicUsize, Ordering};

use serde::Deserialize;

/// The OS gram is running on, i.e. the target a plain `gram build` builds for.
pub fn host() -> &'static str {
    std::env::consts::OS
}

/// A target this host can't build at all (linux and macos have no cross toolchain to each
/// other; Windows is only reachable as a cross target, never as a source of one here).
pub fn unbuildable_target() -> &'static str {
    if host() == "linux" { "macos" } else { "linux" }
}

/// File name of an executable built for the host.
pub fn exe(name: &str) -> String {
    if host() == "windows" { format!("{name}.exe") } else { name.to_owned() }
}

/// File name of a cmake SHARED library built for the host.
pub fn shared_lib(name: &str) -> String {
    match host() {
        "windows" => format!("{name}.dll"),
        "macos"   => format!("lib{name}.dylib"),
        _         => format!("lib{name}.so"),
    }
}

pub struct Sandbox {
    /// The temporary directory, deleted when the test ends.
    root: PathBuf,
    /// Where gram runs: the project directory once there is one, `root` before that.
    cwd: PathBuf,
}

impl Sandbox {
    /// An empty directory, nothing in it yet.
    pub fn empty() -> Self {
        static COUNTER: AtomicUsize = AtomicUsize::new(0);
        let root = std::env::temp_dir().join(format!(
            "gram-test-{}-{}",
            std::process::id(),
            COUNTER.fetch_add(1, Ordering::SeqCst)
        ));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).expect("create the sandbox directory");
        Sandbox { cwd: root.clone(), root }
    }

    /// `gram init <name> <init_args...>` in a fresh sandbox; gram then runs inside the new
    /// project, the way you'd `cd` into it after init.
    pub fn project(name: &str, init_args: &[&str]) -> Self {
        let mut sandbox = Self::empty();
        let mut args = vec!["init", name];
        args.extend_from_slice(init_args);
        sandbox.gram(&args).succeeds();
        sandbox.cwd = sandbox.root.join(name);
        sandbox
    }

    /// Runs `gram <args...>` in the project directory.
    pub fn gram(&self, args: &[&str]) -> Run {
        self.gram_with_env(args, &[])
    }

    /// Same as `gram`, with extra environment variables. `VCPKG_ROOT` is removed unless set
    /// here, so a developer's own vcpkg setup can't change what a test sees.
    pub fn gram_with_env(&self, args: &[&str], env: &[(&str, &str)]) -> Run {
        let mut command = Command::new(env!("CARGO_BIN_EXE_gram"));
        command.args(args).current_dir(&self.cwd).env_remove("VCPKG_ROOT");
        for (key, value) in env {
            command.env(key, value);
        }
        let output = command.output().expect("run the gram binary");
        Run {
            args: args.join(" "),
            code: output.status.code(),
            stdout: String::from_utf8_lossy(&output.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&output.stderr).into_owned(),
        }
    }

    /// Absolute path of a file inside the project.
    pub fn path(&self, relative: &str) -> PathBuf {
        self.cwd.join(relative)
    }

    pub fn exists(&self, relative: &str) -> bool {
        self.path(relative).exists()
    }

    #[track_caller]
    pub fn read(&self, relative: &str) -> String {
        fs::read_to_string(self.path(relative))
            .unwrap_or_else(|err| panic!("couldn't read {relative}: {err}"))
    }

    pub fn write(&self, relative: &str, content: &str) {
        let path = self.path(relative);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, content).unwrap();
    }

    /// Replaces `from` with `to` in a project file, failing loudly if `from` isn't there
    /// (a silent no-op would make the test check the wrong thing).
    #[track_caller]
    pub fn edit(&self, relative: &str, from: &str, to: &str) {
        let content = self.read(relative);
        assert!(content.contains(from), "{relative} doesn't contain {from:?}:\n{content}");
        self.write(relative, &content.replacen(from, to, 1));
    }

    /// The project's Gram.ron, parsed.
    #[track_caller]
    pub fn config(&self) -> BuildConfig {
        ron::from_str(&self.read("Gram.ron")).expect("Gram.ron is valid RON")
    }

    /// A nested project's Gram.ron (`dir/Gram.ron`), parsed.
    #[track_caller]
    pub fn config_at(&self, dir: &str) -> BuildConfig {
        ron::from_str(&self.read(&format!("{dir}/Gram.ron"))).expect("Gram.ron is valid RON")
    }
}

impl Drop for Sandbox {
    fn drop(&mut self) {
        if std::thread::panicking() {
            eprintln!("sandbox kept for inspection: {}", self.root.display());
        } else {
            let _ = fs::remove_dir_all(&self.root);
        }
    }
}

/// The result of one `gram` invocation.
pub struct Run {
    pub args: String,
    pub code: Option<i32>,
    pub stdout: String,
    pub stderr: String,
}

impl Run {
    /// Fails the test (showing everything gram printed) unless gram exited with 0.
    #[track_caller]
    pub fn succeeds(self) -> Self {
        assert_eq!(self.code, Some(0), "`gram {}` should succeed\n{}", self.args, self.dump());
        self
    }

    /// Fails the test unless gram exited with an error whose message contains `message`.
    #[track_caller]
    pub fn fails_with(self, message: &str) -> Self {
        assert_ne!(self.code, Some(0), "`gram {}` should fail\n{}", self.args, self.dump());
        assert!(
            self.stderr.contains(message),
            "`gram {}` should fail with {message:?}\n{}", self.args, self.dump()
        );
        self
    }

    /// Whether gram printed `text`, on either stdout or stderr.
    pub fn printed(&self, text: &str) -> bool {
        self.stdout.contains(text) || self.stderr.contains(text)
    }

    fn dump(&self) -> String {
        format!("exit code: {:?}\n--- stdout ---\n{}\n--- stderr ---\n{}", self.code, self.stdout, self.stderr)
    }
}

/// Asserts that `text` contains `expected`, printing the whole text if it doesn't.
#[track_caller]
pub fn assert_contains(text: &str, expected: &str) {
    assert!(text.contains(expected), "expected to find {expected:?} in:\n{text}");
}

#[track_caller]
pub fn assert_not_contains(text: &str, unexpected: &str) {
    assert!(!text.contains(unexpected), "expected NOT to find {unexpected:?} in:\n{text}");
}

// The tests' own view of Gram.ron. It mirrors the file format rather than reusing gram's
// structs, so a change to the format shows up here as a deliberate edit.

#[derive(Debug, Deserialize)]
pub struct BuildConfig {
    pub name: String,
    pub version: String,
    #[serde(default)]
    pub main: String,
    pub modules: Vec<ModuleConfig>,
}

#[derive(Debug, Deserialize)]
pub struct ModuleConfig {
    pub name: String,
    pub lang: String,
    #[serde(default)]
    pub kind: String,
    pub src: String,
    pub deps: Vec<DependencySpec>,
    pub targets: Vec<String>,
}

/// Where a dependency comes from, written into Gram.ron as e.g. `Git(name: "fmt", url: ..., tag: ...)`.
#[derive(Debug, Deserialize, PartialEq)]
pub enum DependencySpec {
    Registry { name: String },
    Git { name: String, url: String, tag: String },
    Vcpkg { name: String },
    Module { name: String },
}

pub fn registry(name: &str) -> DependencySpec {
    DependencySpec::Registry { name: name.to_owned() }
}

pub fn git(name: &str, url: &str, tag: &str) -> DependencySpec {
    DependencySpec::Git { name: name.to_owned(), url: url.to_owned(), tag: tag.to_owned() }
}

pub fn vcpkg(name: &str) -> DependencySpec {
    DependencySpec::Vcpkg { name: name.to_owned() }
}

pub fn module_ref(name: &str) -> DependencySpec {
    DependencySpec::Module { name: name.to_owned() }
}

impl BuildConfig {
    #[track_caller]
    pub fn module(&self, name: &str) -> &ModuleConfig {
        self.modules.iter().find(|module| module.name == name)
            .unwrap_or_else(|| panic!("Gram.ron has no module {name:?}: {self:#?}"))
    }

    pub fn has_module(&self, name: &str) -> bool {
        self.modules.iter().any(|module| module.name == name)
    }
}

/// A stand-in for a vcpkg installation: an empty toolchain file, so cmake configures without
/// vcpkg installing anything (and without network). Lets tests check what gram passes to
/// vcpkg, not vcpkg itself.
pub fn fake_vcpkg_root(sandbox: &Sandbox) -> PathBuf {
    let root = sandbox.root.join("fake-vcpkg");
    let toolchain = root.join("scripts/buildsystems/vcpkg.cmake");
    fs::create_dir_all(toolchain.parent().unwrap()).unwrap();
    fs::write(&toolchain, "# empty: stands in for vcpkg in gram's tests\n").unwrap();
    root
}

pub fn display(path: &Path) -> String {
    path.display().to_string()
}
