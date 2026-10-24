use std::fs;
use std::io;
use std::path::Path;
use std::process::Command;
use serde::{Deserialize, Serialize};

use crate::config::BuildConfig;
use crate::directory::Directory;
use crate::lang::DependencySpec;
use crate::target::buildable_on_host;

fn default_kind() -> String {
    String::from("runtime")
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ModuleConfig {
    pub(crate) name: String,
    pub(crate) lang: String,
    /// "runtime" (executable) or "library" (dynamic). Only meaningful for lang == "cmake".
    #[serde(default = "default_kind")]
    pub(crate) kind: String,
    pub(crate) src: String,
    pub(crate) deps: Vec<String>,
    pub(crate) targets: Vec<String>
}

impl ModuleConfig {
    pub fn add_dependency(&mut self, dependency: &str, url: &str, tag: &str, vcpkg: bool) -> io::Result<()> {
        let name = dependency.to_owned();
        let dep = if vcpkg {
            if !url.is_empty() || !tag.is_empty() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "--vcpkg can't be combined with --url/--tag (those are FetchContent-only)",
                ));
            }
            DependencySpec::Vcpkg { name }
        } else if url.is_empty() && tag.is_empty() {
            DependencySpec::Registry { name }
        } else {
            DependencySpec::Git { name, url: url.to_owned(), tag: tag.to_owned() }
        };

        self.backend()?.add_dependency(self, &dep)
    }

    /// Adds another module already in Gram.ron as a dependency (same-language only,
    /// enforced by the caller): a cargo path dependency, or a cmake add_subdirectory +
    /// target_link_libraries. Unlike an external dependency, gram knows the exact target
    /// name here (it generated it), so it can wire the link itself.
    pub fn add_module_ref(&mut self, dependency: &str) -> io::Result<()> {
        self.backend()?.add_module_ref(self, dependency)
    }

    /// Whether the module already lists `dependency`, whatever its source (registry, git,
    /// vcpkg or module ref): a module depends on a name at most once.
    pub(crate) fn has_dependency(&self, dependency: &str) -> bool {
        self.deps.iter().any(|dep| dep == dependency)
    }

    pub fn rm_dependency(&mut self, dependency: &str) -> io::Result<String> {
        self.backend()?.rm_dependency(self, dependency)
    }

    pub fn build(&self, project: &BuildConfig, ci: &bool, target: &str) -> io::Result<()> {
        let name = self.name.clone();
        let lang = self.lang.clone();
        println!("gram module (name={name}, lang={lang}, ci={ci})");

        match ci {
            true => {
                for target in &self.targets {
                    if !buildable_on_host(target) {
                        eprintln!(
                            "warning: skipping module {name} target \"{target}\" — can't be built from a {} host",
                            std::env::consts::OS
                        );
                        continue;
                    }
                    self.build_target(project, ci, target)?;
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
                if !buildable_on_host(target) {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("can't build target {target} from a {} host", std::env::consts::OS),
                    ));
                }
                self.build_target(project, ci, target)
            },
        }
    }

    fn build_target(&self, project: &BuildConfig, ci: &bool, target: &str) -> io::Result<()> {
        let name = self.name.clone();
        println!("gram target (name={name}, target={target})");
        
        let project_name = project.name.clone();
        let builds = match ci {
            true => {
                let version = project.version.clone();
                format!("build/{project_name}_{version}_{target}")
            },
            false => format!("build/{project_name}/{target}"),
        };

        let backend = self.backend()?;
        fs::create_dir_all(&builds)?;
        let (prefix, ext) = backend.artifact_naming(self, target);
        let out = format!("{builds}/{prefix}{name}{ext}");

        backend.compile(self, &out, target)
    }

    pub fn run(&self, project: &str, target: &str, args: &[String]) -> io::Result<i32> {
        if self.kind != "runtime" {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("module {} is a \"{}\" module, not runnable (only \"runtime\" modules are)", self.name, self.kind),
            ));
        }

        let (prefix, ext) = self.backend()?.artifact_naming(self, target);
        let path = format!("build/{project}/{target}/{prefix}{}{ext}", self.name);

        if !Path::new(&path).exists() {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("build artifact not found at {path} — run `gram build` first"),
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
