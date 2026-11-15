//! A `gram` module is another Gram project (its `src` is that project's Gram.ron). It builds
//! nothing itself: `BuildConfig::open` loads its modules next to the parent's, so they're
//! built, referenced and edited like any other module.

use std::io;

use crate::config::BuildConfig;
use crate::config::module::ModuleConfig;
use crate::directory::Directory;
use crate::lang::DependencySpec;
use crate::lang::Gram;
use crate::lang::ModuleBackend;
use crate::lang::ResolvedModule;
use crate::profile::Profile;
use crate::toolchain::ToolchainSettings;

const NOT_A_TARGET: &str = "a gram module is a collection of modules: add dependencies to one of its modules instead";

impl ModuleBackend for Gram {
    fn manifest_file(&self) -> &'static str {
        "Gram.ron"
    }

    fn scaffold(&self, module: &ModuleConfig, project: &Directory) -> io::Result<()> {
        Directory::new(project.path.join(module.dir()))?
            .write_file(self.manifest_file(), &BuildConfig::new(&module.name, "0.1.0")?)
    }

    fn compile(&self, _module: &ModuleConfig, _refs: &[ResolvedModule], _out: &str, _target: &str, _profile: Profile, _toolchain: &ToolchainSettings) -> io::Result<()> {
        // Its modules are in the same build list as the parent's and get built on their own.
        Ok(())
    }

    fn artifact_naming(&self, _module: &ModuleConfig, _target: &str) -> (&'static str, &'static str) {
        ("", "")
    }

    fn add_dependency(&self, _module: &mut ModuleConfig, _dep: &DependencySpec) -> io::Result<()> {
        Err(io::Error::new(io::ErrorKind::InvalidInput, NOT_A_TARGET))
    }

    fn rm_dependency(&self, _module: &mut ModuleConfig, _dependency: &str) -> io::Result<String> {
        Err(io::Error::new(io::ErrorKind::InvalidInput, NOT_A_TARGET))
    }
}
