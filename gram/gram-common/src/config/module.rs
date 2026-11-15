use std::fs;
use std::io;
use std::path::Path;
use std::process::Command;
use serde::{Deserialize, Serialize};

use crate::config::BuildConfig;
use crate::directory::Directory;
use crate::lang::DependencySpec;
use crate::lang::ResolvedModule;
use crate::profile::Profile;
use crate::toolchain::{self, Toolchain, ToolchainSettings};

fn default_kind() -> String {
    String::from("runtime")
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ModuleConfig {
    pub(crate) name: String,
    pub(crate) lang: String,
    /// "runtime" (executable), "library" (linked into another module: a cmake library, or an
    /// rlib for cargo) or "dynamic" (cargo only: a cdylib, loaded at runtime by the executable
    /// and copied next to it).
    #[serde(default = "default_kind")]
    pub(crate) kind: String,
    pub(crate) src: String,
    pub(crate) deps: Vec<DependencySpec>,
    pub(crate) targets: Vec<String>,
    /// Directory of the Gram.ron this module is declared in, relative to the root project
    /// ("" for the root itself). `src` is kept relative to the root in memory; this is what
    /// lets `BuildConfig::write` put the module back in its own file.
    #[serde(skip)]
    pub(crate) project: String,
}

impl ModuleConfig {
    pub fn add_dependency(&mut self, dependency: &DependencySpec) -> io::Result<()> {
        self.backend()?.add_dependency(self, dependency)
    }

    /// Adds another module already in Gram.ron as a dependency (same-language only,
    /// enforced by the caller): a cargo path dependency, or a cmake add_subdirectory +
    /// target_link_libraries. Unlike an external dependency, gram knows the exact target
    /// name here (it generated it), so it can wire the link itself.
    pub(crate) fn add_module_ref(&mut self, resolved: &ResolvedModule) -> io::Result<()> {
        self.backend()?.add_module_ref(self, resolved)
    }

    /// Whether the module already lists `dependency`, whatever its source (registry, git,
    /// vcpkg or module ref): a module depends on a name at most once.
    pub(crate) fn has_dependency(&self, dependency: &DependencySpec) -> bool {
        self.deps.iter().any(|dep| dep.name() == dependency.name())
    }

    pub fn rm_dependency(&mut self, dependency: &str) -> io::Result<String> {
        self.backend()?.rm_dependency(self, dependency)
    }

    /// `toolchain` is the one `--toolchain` asked for (never with `--ci`), or None for the host's default.
    pub fn build(&self, project: &BuildConfig, ci: &bool, target: &str, profile: Profile, toolchain: Option<Toolchain>) -> io::Result<()> {
        let name = self.name.clone();
        let lang = self.lang.clone();
        println!("gram module (name={name}, lang={lang}, ci={ci}, config={profile})");

        match ci {
            true => {
                for target in &self.targets {
                    let settings = match self.toolchain_for(target, None) {
                        Ok(settings) => settings,
                        Err(err) if err.kind() == io::ErrorKind::Unsupported => {
                            eprintln!(
                                "warning: skipping module {name} target \"{target}\" — can't be built from a {} host",
                                std::env::consts::OS
                            );
                            continue;
                        }
                        Err(err) => return Err(err),
                    };
                    self.build_target(project, ci, target, profile, &settings)?;
                }
                Ok(())
            },
            false => {
                let target = if target.is_empty() { std::env::consts::OS } else { target };
                if !self.targets.iter().any(|t| t == target) {
                    eprintln!(
                        "warning: module {name} does not declare \"{target}\" as a supported target (declared: {:?})",
                        self.targets
                    );
                }
                let settings = self.toolchain_for(target, toolchain)?;
                self.build_target(project, ci, target, profile, &settings)
            },
        }
    }

    /// The toolchain building `target` from this host (toolchain.rs: `resolve`, then its `settings`).
    /// A fallback (mingw on Windows without Visual Studio) is only a warning: the build goes on.
    fn toolchain_for(&self, target: &str, requested: Option<Toolchain>) -> io::Result<ToolchainSettings> {
        let host = std::env::consts::OS;
        let resolution = toolchain::resolve(host, target, requested, &toolchain::installed)?;
        if let Some(preferred) = resolution.fell_back_from {
            eprintln!(
                "warning: module {}: {preferred} isn't installed, building {target} with {} instead",
                self.name, resolution.toolchain
            );
        }
        Ok(toolchain::settings(host, target, resolution.toolchain))
    }

    fn build_target(&self, project: &BuildConfig, ci: &bool, target: &str, profile: Profile, toolchain: &ToolchainSettings) -> io::Result<()> {
        let name = self.name.clone();
        println!("gram target (name={name}, target={target}, config={profile})");

        let builds = output_dir(project, *ci, target, profile);

        let backend = self.backend()?;
        fs::create_dir_all(&builds)?;
        let (prefix, ext) = backend.artifact_naming(self, target);
        let out = format!("{builds}/{prefix}{}{ext}", project.artifact_name(self));

        let refs = project.module_refs(self)?;
        backend.compile(self, &refs, &out, target, profile, toolchain)
    }

    pub fn run(&self, project: &BuildConfig, target: &str, profile: Profile, args: &[String]) -> io::Result<i32> {
        if self.lang == "gram" {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("module {} is a gram module (a collection of modules) — run one of its modules instead", self.name),
            ));
        }
        if self.kind != "runtime" {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("module {} is a \"{}\" module, not runnable (only \"runtime\" modules are)", self.name, self.kind),
            ));
        }

        let (prefix, ext) = self.backend()?.artifact_naming(self, target);
        let path = format!("{}/{prefix}{}{ext}", output_dir(project, false, target, profile), project.artifact_name(self));

        if !Path::new(&path).exists() {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("build artifact not found at {path} — run `gram build --config {profile}` first"),
            ));
        }

        println!("gram run (name={}, target={target})", self.name);
        let status = Command::new(&path).args(args).status()?;
        Ok(status.code().unwrap_or(1))
    }

    pub fn save(&self, project: &Directory) -> Result<(), std::io::Error> {
        let name = self.name.clone();
        let lang = self.lang.clone();
        println!("gram module (name={name}, lang={lang})");
        self.backend()?.scaffold(self, project)
    }
}

/// Where a module's artifacts land, relative to the project root: `build/<project>/<target>/<profile>/`,
/// or `build/<project>_<version>_<target>_<profile>/` for a `--ci` build.
fn output_dir(project: &BuildConfig, ci: bool, target: &str, profile: Profile) -> String {
    let name = &project.name;
    match ci {
        true => format!("build/{name}_{}_{target}_{profile}", project.version),
        false => format!("build/{name}/{target}/{profile}"),
    }
}
