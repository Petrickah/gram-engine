use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

use crate::config::module::ModuleConfig;
use crate::directory::Directory;
use crate::lang::Cargo;
use crate::lang::DependencySpec;
use crate::lang::ModuleBackend;
use crate::lang::ResolvedModule;
use crate::lang::slash_path;
use crate::target::cargo_cross_triple;

impl ModuleBackend for Cargo {
    fn artifact_naming(&self, _module: &ModuleConfig, target: &str) -> (&'static str, &'static str) {
        if target == "windows" { ("", ".exe") } else { ("", "") }
    }

    fn rust_analyzer_manifest<'a>(&self, module: &'a ModuleConfig) -> Option<&'a str> {
        Some(&module.src)
    }

    fn manifest_file(&self) -> &'static str {
        "Cargo.toml"
    }

    fn add_module_ref(&self, module: &mut ModuleConfig, resolved: &ResolvedModule) -> io::Result<()> {
        let name = &resolved.name;
        println!("gram add (dependency={name}, src={}): adding module reference (cargo path dependency)", module.src);
        let mut command = Command::new("cargo");
        // cargo reads --path relative to its cwd, which for gram is the project root: hence
        // `dir`, not `rel_dir` (cargo writes the consumer-relative path into Cargo.toml itself).
        command.args(["add", name, "--path", &slash_path(&resolved.dir), "--manifest-path", &module.src]);

        let status = command.status()?;
        match status.success() {
            true => Ok(module.deps.push(DependencySpec::Module { name: resolved.name.clone() })),
            false => Err(io::Error::new(io::ErrorKind::Other, "cargo add (path dependency) failed"))
        }
    }
        
    fn scaffold(&self, module: &ModuleConfig, project: &Directory) -> io::Result<()> {
        let manifest_dir = project.path.join(
            Path::new(&module.src).parent().unwrap_or_else(|| Path::new("."))
        );

        match fs::create_dir_all(&manifest_dir) {
            Ok(_) => {
                let status = match manifest_dir.try_exists()? {
                    true => {
                        let mut command = Command::new("cargo");
                        command.args(["init", "--name", &module.name, "--vcs", "none", "--edition", "2024"]);
                        if module.kind == "library" {
                            // Otherwise `cargo init` always scaffolds src/main.rs (a binary
                            // crate), which has no library target for another module to
                            // reference via --module-ref.
                            command.arg("--lib");
                        }
                        command.current_dir(&manifest_dir);
                        command.status()
                    }
                    false => Err(io::Error::new(io::ErrorKind::AlreadyExists, "module already exists")),
                };
                
                match status {
                    Ok(_) => Ok(()),
                    Err(_) => Err(io::Error::new(io::ErrorKind::Other, "cargo init failed")),
                }
            },
            Err(err) => Err(io::Error::new(io::ErrorKind::AlreadyExists, format!("Error creating module: {err}"))),
        }
    }
    
    fn compile(&self, module: &ModuleConfig, _refs: &[ResolvedModule], out: &str, target: &str) -> io::Result<()> {
        let mut command = Command::new("cargo");
        command.args(["build", "--manifest-path", &module.src.clone()]);
        if let Some(triple) = cargo_cross_triple(target)? {
            command.args(["--target", triple]);
        }

        if module.kind == "library" {
            // No [lib] crate-type management yet (defaults to rlib) — there's no
            // standalone binary to place under build/, just compile it. Revisit once
            // something (Corrosion, Wave 1) needs a specific crate-type artifact here.
            let status = command.status()?;
            return match status.success() {
                true => Ok(()),
                false => Err(io::Error::new(io::ErrorKind::Other, "cargo build failed")),
            };
        }

        // Where the binary ends up isn't derivable from the module alone: a workspace member
        // builds into the workspace root's target/, and the binary is named after the cargo
        // package, not the gram module. So ask cargo instead: with a JSON message format it
        // reports every artifact's real path on stdout, while compiler diagnostics stay
        // human-readable on stderr.
        command.arg("--message-format=json-render-diagnostics").stderr(Stdio::inherit());
        let output = command.output()?;
        if !output.status.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "cargo build failed"));
        }

        let from = find_executable(&output.stdout, &module.name)?;
        fs::copy(from, out).map(|_| ())
    }
    
    fn add_dependency(&self, module: &mut ModuleConfig, dep: &DependencySpec) -> io::Result<()> {
        let DependencySpec::Registry { name } = dep else {
            return Err(io::Error::new(io::ErrorKind::InvalidInput,
                "cargo modules take plain registry dependencies (no --url/--tag/--vcpkg)"));
        };
        println!("gram add (dependency={name}, src={}): adding the new dependency", module.src);

        let mut command = Command::new("cargo");
        command.args(["add", &name.clone(), "--manifest-path", &module.src.clone()]);

        let status = command.status()?;
        match status.success() {
            true => Ok(module.deps.push(dep.clone())),
            false => Err(io::Error::new(io::ErrorKind::Other, "cargo add failed"))
        }
    }
    
    fn rm_dependency(&self, module: &mut ModuleConfig, dependency: &str) -> io::Result<String> {
        println!("gram remove (dependency={dependency}, src={}): removing the dependency", module.src);

        let mut command = Command::new("cargo");
        command.args(["rm", dependency, "--manifest-path", &module.src.clone()]);

        let status = command.status()?;
        match status.success() {
            true => match module.deps.iter().position(|dep| dep.name() == dependency) {
                Some(position) => Ok(module.deps.swap_remove(position).name()),
                None => Err(io::Error::new(io::ErrorKind::Other, "dependency not found")),
            },
            false => Err(io::Error::new(io::ErrorKind::Other, "cargo rm failed"))
        }
    }
}

/// Picks the module's binary out of cargo's JSON build messages: the single executable it
/// produced or, when a manifest builds several (a workspace, or a package with more than one
/// `[[bin]]`), the one named like the module.
fn find_executable(cargo_stdout: &[u8], module: &str) -> io::Result<PathBuf> {
    let executables: Vec<(String, PathBuf)> = String::from_utf8_lossy(cargo_stdout)
        .lines()
        .filter_map(|line| serde_json::from_str::<serde_json::Value>(line).ok())
        .filter(|message| message["reason"] == "compiler-artifact")
        .filter(|message| message["target"]["kind"].as_array().is_some_and(|kinds| kinds.iter().any(|kind| kind == "bin")))
        .filter_map(|message| Some((
            message["target"]["name"].as_str()?.to_owned(),
            PathBuf::from(message["executable"].as_str()?),
        )))
        .collect();

    match executables.as_slice() {
        [(_, path)] => Ok(path.clone()),
        [] => Err(io::Error::new(
            io::ErrorKind::NotFound,
            format!("cargo build produced no executable for module {module}"),
        )),
        several => several.iter()
            .find(|(name, _)| name == module)
            .map(|(_, path)| path.clone())
            .ok_or_else(|| io::Error::new(
                io::ErrorKind::NotFound,
                format!(
                    "cargo build produced several executables ({}) and none is named like module {module}",
                    several.iter().map(|(name, _)| name.as_str()).collect::<Vec<_>>().join(", ")
                ),
            )),
    }
}
