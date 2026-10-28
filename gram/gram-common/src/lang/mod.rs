//! Per-language module support. Each module language (`lang` in Gram.ron) is a stateless
//! `ModuleBackend`; `backend_for` is the only place that maps a language name to one.
//! Adding a language means a new file with `impl ModuleBackend` plus one line there.

use std::io;
use std::path::Path;
use std::path::PathBuf;
use serde::Deserialize;
use serde::Serialize;

use crate::BuildConfig;
use crate::Directory;
use crate::ModuleConfig;

pub mod cargo;
pub mod cmake;
pub mod gram;
pub mod vcpkg;

/// A `DependencySpec::Module` looked up in Gram.ron: everything a backend needs to wire the
/// reference. Computed on demand, never serialized — Gram.ron keeps only the name.
pub(crate) struct ResolvedModule {
    pub name: String,
    pub lang: String,
    pub kind: String,
    /// The module's directory, relative to the project root (gram's cwd).
    pub dir: PathBuf,
    /// The same directory, relative to the consumer module's directory.
    pub rel_dir: PathBuf,
}


/// Where a dependency comes from. Built from the `gram add` flags; each backend accepts
/// the kinds it supports and rejects the rest.
#[derive(Debug, Serialize, Deserialize, PartialEq, Clone)]
pub enum DependencySpec {
    /// The language's own package registry (crates.io for cargo).
    Registry { name: String },
    /// A git repository, fetched with CMake's FetchContent.
    Git { name: String, url: String, tag: String },
    /// A vcpkg port, recorded in the module's vcpkg.json.
    Vcpkg { name: String },
    /// Another module of the same project (`--module-ref`).
    Module { name: String },
}

pub(crate) trait ModuleBackend {
    /// The manifest a new module of this language is created around, relative to its
    /// directory: becomes `ModuleConfig::src` as `<name>/<manifest_file>`.
    fn manifest_file(&self) -> &'static str;
    /// Creates the module's files on disk (`gram init` / `gram add` of a new module).
    fn scaffold(&self, module: &ModuleConfig, project: &Directory) -> io::Result<()>;
    /// Builds the module for `target` and places its artifact at `out`.
    /// `refs` are the module's `DependencySpec::Module` dependencies, already resolved.
    fn compile(&self, module: &ModuleConfig, refs: &[ResolvedModule], out: &str, target: &str) -> io::Result<()>;
    /// (filename prefix, extension) of the module's build artifact on `target`.
    fn artifact_naming(&self, module: &ModuleConfig, target: &str) -> (&'static str, &'static str);
    fn add_dependency(&self, module: &mut ModuleConfig, dep: &DependencySpec) -> io::Result<()>;
    fn rm_dependency(&self, module: &mut ModuleConfig, dependency: &str) -> io::Result<String>;

    // Default implementations: a backend only overrides what it supports.

    /// Depends on another module of the same project (same language, a library).
    fn add_module_ref(&self, _module: &mut ModuleConfig, _resolved: &ResolvedModule) -> io::Result<()> {
        Err(io::Error::new(io::ErrorKind::InvalidInput, "module references aren't supported for this language"))
    }
    /// The manifest rust-analyzer should load for this module, if any (`.vscode/settings.json`).
    fn rust_analyzer_manifest<'a>(&self, _module: &'a ModuleConfig) -> Option<&'a str> {
        None
    }
}

// Zero-sized: no data, behavior only. The module's data stays in ModuleConfig.
pub(crate) struct Cargo;
pub(crate) struct CMake;
pub(crate) struct Gram;

pub(crate) fn backend_for(lang: &str) -> io::Result<&'static dyn ModuleBackend> {
    match lang {
        "cargo" => Ok(&Cargo),
        "cmake" => Ok(&CMake),
        "gram"  => Ok(&Gram),
        other   => Err(io::Error::new(io::ErrorKind::InvalidInput, format!("unsupported module language: {other}"))),
    }
}

impl ModuleConfig {
    pub(crate) fn backend(&self) -> io::Result<&'static dyn ModuleBackend> {
        backend_for(&self.lang)
    }

    /// The module's directory, relative to the project root: where its manifest (`src`) lives.
    pub(crate) fn dir(&self) -> &Path {
        Path::new(&self.src).parent().unwrap_or(Path::new(""))
    }
}

impl BuildConfig {
    pub(crate) fn resolve(&self, consumer: &str, dependency: &str) -> io::Result<ResolvedModule> {
        let find = |name: &str| self.modules.iter().find(|m| m.name == name);
        let consumer = find(consumer).expect("the consumer module exists by now");
        let dep = find(dependency).ok_or_else(|| io::Error::new(
            io::ErrorKind::NotFound,
            format!("referenced module not found: {dependency} (create it first, e.g. `gram add --module {dependency} --lang ...`)"),
        ))?;

        Ok(ResolvedModule {
            name: dep.name.clone(),
            lang: dep.lang.clone(),
            kind: dep.kind.clone(),
            dir: dep.dir().to_owned(),
            rel_dir: relative_dir(consumer.dir(), dep.dir()),
        })
    }

    /// Every module `consumer` references (its `DependencySpec::Module` deps), resolved.
    pub(crate) fn module_refs(&self, consumer: &ModuleConfig) -> io::Result<Vec<ResolvedModule>> {
        consumer.deps.iter()
            .filter_map(|dep| match dep {
                DependencySpec::Module { name } => Some(self.resolve(&consumer.name, name)),
                _ => None,
            })
            .collect()
    }
}

impl DependencySpec {
    pub(crate) fn new(dependency: &str, url: &str, tag: &str, vcpkg: bool, module_ref: bool) -> io::Result<Self> {
        let name = dependency.to_owned();
        if module_ref {
            if !url.is_empty() || !tag.is_empty() || vcpkg {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "--module-ref can't be combined with --url/--tag/--vcpkg (those are for external dependencies)",
                ));
            }
            Ok(DependencySpec::Module { name })
        } else if vcpkg {
            if !url.is_empty() || !tag.is_empty() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "--vcpkg can't be combined with --url/--tag (those are FetchContent-only)",
                ));
            }
            Ok(DependencySpec::Vcpkg { name })
        } else if url.is_empty() && tag.is_empty() {
            Ok(DependencySpec::Registry { name })
        } else {
            Ok(DependencySpec::Git { name, url: url.to_owned(), tag: tag.to_owned() })
        }
    }

    pub fn name(&self) -> String {
        match self {
            DependencySpec::Registry { name } => name.clone(),
            DependencySpec::Git { name, .. } => name.clone(),
            DependencySpec::Vcpkg { name } => name.clone(),
            DependencySpec::Module { name } => name.clone(),
        }
    }
}

/// `to` as seen from `from`, both relative to the project root (no filesystem access, so it
/// works before anything exists on disk).
fn relative_dir(from: &Path, to: &Path) -> PathBuf {
    let from: Vec<_> = from.components().collect();
    let to: Vec<_> = to.components().collect();
    let common = from.iter().zip(&to).take_while(|(a, b)| a == b).count();

    let mut rel = PathBuf::new();
    for _ in common..from.len() { rel.push(".."); }
    for component in &to[common..] { rel.push(component); }
    rel
}

/// A relative path with `/` separators whatever the host (CMake and Cargo.toml both want those,
/// while `Path::display` would give `\` on Windows).
pub(crate) fn slash_path(path: &Path) -> String {
    path.components()
        .map(|component| component.as_os_str().to_string_lossy())
        .collect::<Vec<_>>()
        .join("/")
}
