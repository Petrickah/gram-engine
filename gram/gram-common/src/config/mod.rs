use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use serde::{Deserialize, Serialize};

use crate::commands::add::AddCommand;
use crate::commands::remove::RemoveCommand;
use crate::commands::run::RunCommand;
use crate::directory::Directory;
use crate::lang::{DependencySpec, backend_for};
use crate::target::validate_targets;

pub mod module;

use module::ModuleConfig;

/// What a `gram add` actually did, so the CLI can say so (one command can create a module,
/// add targets, add a dependency, or find there's nothing to do).
#[derive(Debug, PartialEq)]
pub enum AddOutcome {
    /// No dependency given; the module didn't exist and was created.
    ModuleCreated { module: String },
    /// No dependency given; the existing module now declares these extra targets.
    TargetsAdded { module: String, targets: Vec<String> },
    /// The dependency was added (the module was created first if it didn't exist).
    DependencyAdded { module: String, dependency: DependencySpec },
    /// The module already had this dependency; nothing was run or written.
    AlreadyPresent { module: String, dependency: DependencySpec },
    /// No dependency given, and the module already existed with every requested target.
    NothingToDo { module: String },
}

#[derive(Debug, Serialize, Deserialize)]
pub struct BuildConfig {
    pub(crate) name: String,
    pub(crate) version: String,
    /// Name of the project's main runtime module, set once by `gram init`. `gram run`
    /// falls back to this when --module is omitted. Empty (the default for a
    /// Gram.ron predating this field) means there's no implicit default.
    #[serde(default)]
    pub(crate) main: String,
    pub(crate) modules: Vec<ModuleConfig>
}

impl BuildConfig {
    pub fn new(name: &str, version: &str) -> io::Result<Self> {
        Ok(Self {
            name: String::from(name),
            version: String::from(version),
            main: String::new(),
            modules: vec![]
        })
    }

    pub fn set_main(&mut self, module: &str) {
        self.main = String::from(module);
    }

    pub fn open(path: &str) -> io::Result<Self> {
        let path = PathBuf::new().join(path);
        let mut file = fs::File::open(path.join("Gram.ron"))?;
        let mut contents = String::new();
        file.read_to_string(&mut contents)?;

        match ron::de::from_bytes(&contents.as_bytes()) {
            Ok(project) => Ok(project),
            Err(err) => Err(io::Error::new(io::ErrorKind::InvalidData, err)),
        }
    }

    pub fn rm_module(&mut self, name: &str) -> io::Result<ModuleConfig> {
        match self.modules.iter().position(|module| module.name == name) {
            result => match result {
                Some(index) => Ok(self.modules.swap_remove(index)),
                None => Err(io::Error::new(io::ErrorKind::Other, "module not found")),
            },
        }
    }

    pub fn add_module(&mut self, name: &str, lang: &str, kind: &str, targets: &[String]) -> io::Result<()> {
        let targets = if targets.is_empty() {
            vec![String::from(std::env::consts::OS)]
        } else {
            validate_targets(targets)?;
            targets.to_vec()
        };

        let manifest = backend_for(lang)?.manifest_file();
        self.modules.push(ModuleConfig {
            name: String::from(name),
            lang: String::from(lang),
            kind: String::from(kind),
            src: format!("{name}/{manifest}"),
            deps: vec![],
            targets
        });
        Ok(())
    }

    pub fn run(&self, command: RunCommand) -> io::Result<i32> {
        let module = if command.module.is_empty() { self.main.as_str() } else { &command.module };
        if module.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "no --module given, and this project has no main module set (only `gram init` sets one) — pass --module explicitly",
            ));
        }

        let target = if command.target.is_empty() { std::env::consts::OS } else { &command.target };

        self.modules.iter()
            .find(|m| m.name == module)
            .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, format!("module not found: {module}")))?
            .run(&self.name, target, &command.args)
    }

    pub fn build(&self, ci: &bool, target: &str) -> io::Result<()> {
        if *ci && !target.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "--target can't be combined with --ci — --ci already builds every target declared in a module's targets",
            ));
        }
        if !target.is_empty() {
            validate_targets(std::slice::from_ref(&target.to_string()))?;
        }

        let name = self.name.clone();
        println!("gram build (name={name}, ci={ci})");
        for curr_module in &self.modules {
            curr_module.build(&self, &ci, target)?;
        }
        Ok(())
    }

    pub fn add_dependency(&mut self, command: AddCommand) -> io::Result<AddOutcome> {
        let project = Directory::new(".")?;
        let lang = command.lang;
        let targets = command.target;
        let module = command.module;
        let created = !self.modules.iter().any(|m| m.name == module);
        let mut added_targets: Vec<String> = vec![];

        if created {
            println!("gram module (name={module}, lang={lang}) created");
            self.add_module(&module, &lang, &command.kind, &targets)?;
            self.modules.last().unwrap().save(&project)?;
            project.write_file("Gram.ron", self)?;
            self.write_vscode_settings(&project)?;
        } else if !targets.is_empty() {
            validate_targets(&targets)?;
            let existing = self.modules.iter_mut().find(|m| m.name == module).expect("checked above");
            let added: Vec<&String> = targets.iter().filter(|t| !existing.targets.contains(t)).collect();
            if !added.is_empty() {
                let added_display = added.iter().map(|t| t.as_str()).collect::<Vec<_>>().join(", ");
                added_targets = added.into_iter().cloned().collect();
                existing.targets.extend(added_targets.iter().cloned());
                println!("gram module (name={module}): added target(s) {added_display}");
                project.write_file("Gram.ron", self)?;
            }
        }

        let dependency = DependencySpec::new(&command.name, &command.url, &command.tag, command.vcpkg, command.module_ref)?;
        let name = dependency.name();

        if name.is_empty() {
            return Ok(if created {
                AddOutcome::ModuleCreated { module }
            } else if !added_targets.is_empty() {
                AddOutcome::TargetsAdded { module, targets: added_targets }
            } else {
                AddOutcome::NothingToDo { module }
            });
        }

        // Checked before anything else about the dependency: adding one the module already has
        // is a no-op (no second `cargo add`, no second CMake block, no duplicate in Gram.ron).
        let consumer = self.modules.iter().find(|m| m.name == module).expect("module was just created above, or already existed");
        if consumer.has_dependency(&dependency) {
            return Ok(AddOutcome::AlreadyPresent { module, dependency });
        }

        if let DependencySpec::Module { .. } = dependency {
            if *name == module {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "a module can't reference itself"));
            }

            let dep = self.modules.iter().find(|m| *name == m.name).ok_or_else(|| io::Error::new(
                io::ErrorKind::NotFound,
                format!("referenced module not found: {name} (create it first, e.g. `gram add --module {name} --lang ...`)"),
            ))?;
            if dep.kind != "library" {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!("referenced module {name} is a \"{}\" module, not a library — only library modules can be referenced", dep.kind),
                ));
            }
            let dep_lang = dep.lang.clone();

            let consumer = self.modules.iter().find(|m| m.name == module).expect("module was just created above, or already existed");
            if consumer.lang != dep_lang {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!(
                        "cross-language module references aren't supported yet ({module} is \"{}\", {name} is \"{dep_lang}\")",
                        consumer.lang
                    ),
                ));
            }

            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("checked above")
                .add_module_ref(&dependency.clone())?;
        } else {
            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("module was just created above, or already existed")
                .add_dependency(&dependency.clone())?;
        }

        project.write_file("Gram.ron", self)?;
        Ok(AddOutcome::DependencyAdded { module, dependency })
    }

    pub fn rm_dependency(&mut self, command: RemoveCommand) -> io::Result<()> {
        let project = Directory::new(".")?;
        let module     = command.module;
        let dependency = command.name;

        let Some(existing) = self.modules.iter_mut().find(|m| m.name == module) else {
            return Err(io::Error::new(io::ErrorKind::NotFound, format!("module not found: {module}")));
        };

        if !dependency.is_empty() {
            existing.rm_dependency(&dependency)?;
            return project.write_file("Gram.ron", self);
        }

        let removed = self.rm_module(&module)?;
        if self.main == module {
            self.main = String::new();
        }
        project.write_file("Gram.ron", self)?;
        self.write_vscode_settings(&project)?;

        if command.purge {
            let module_dir = Path::new(&removed.src).parent().unwrap_or_else(|| Path::new("."));
            fs::remove_dir_all(module_dir)?;
            println!("gram module (name={module}) removed, {} deleted", module_dir.display());
        } else {
            println!("gram module (name={module}) removed");
        }

        Ok(())
    }

    pub fn save(&mut self, path: &str) -> io::Result<()> {
        let name = self.name.clone();
        let project = Directory::new(path)?;
        project.write_file("Gram.ron", self)?;
        println!("gram init (name={name}, path={path})");
        for curr_module in &self.modules {
            curr_module.save(&project)?;
        }
        self.write_vscode_settings(&project)
    }
}
