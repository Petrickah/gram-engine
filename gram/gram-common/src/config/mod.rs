use std::fs;
use std::collections::HashSet;
use std::io;
use std::path::{Path, PathBuf};
use serde::{Deserialize, Serialize};

use crate::commands::add::AddCommand;
use crate::commands::remove::RemoveCommand;
use crate::commands::run::RunCommand;
use crate::directory::Directory;
use crate::lang::{DependencySpec, backend_for, slash_path};
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

/// A nested Gram project (a `gram` module) as read from its own Gram.ron: kept so `write` can
/// put back what isn't stored on its modules.
#[derive(Debug)]
struct SubProject {
    /// Its directory, relative to the root project.
    dir: String,
    name: String,
    version: String,
    main: String,
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
    /// Every module of the project, nested projects' included (see `open`).
    pub(crate) modules: Vec<ModuleConfig>,
    #[serde(skip)]
    subprojects: Vec<SubProject>,
}

impl BuildConfig {
    pub fn new(name: &str, version: &str) -> io::Result<Self> {
        Ok(Self {
            name: String::from(name),
            version: String::from(version),
            main: String::new(),
            modules: vec![],
            subprojects: vec![],
        })
    }

    pub fn set_main(&mut self, module: &str) {
        self.main = String::from(module);
    }

    /// Reads the project's Gram.ron, plus the Gram.ron of every `gram` module in it, recursively.
    /// A nested project's modules join `modules` with their `src` prefixed by the project's
    /// directory, so everything else (build, references, `cargo`/`cmake` run from the root)
    /// sees one flat list of modules relative to the root. Module names are unique across it.
    pub fn open(path: &str) -> io::Result<Self> {
        let root = PathBuf::new().join(path);
        let mut project = Self::read(&root.join("Gram.ron"))?;

        // Appended modules get visited too, which is what makes nesting recursive.
        let mut index = 0;
        while index < project.modules.len() {
            let module = &project.modules[index];
            index += 1;
            if module.lang != "gram" {
                continue;
            }

            let dir = slash_path(module.dir());
            let nested = Self::read(&root.join(&module.src))?;
            project.subprojects.push(SubProject {
                dir: dir.clone(),
                name: nested.name,
                version: nested.version,
                main: nested.main,
            });
            for mut module in nested.modules {
                module.src = slash_path(&Path::new(&dir).join(&module.src));
                module.project = dir.clone();
                project.modules.push(module);
            }
        }

        let mut names = HashSet::new();
        if let Some(duplicate) = project.modules.iter().find(|m| !names.insert(m.name.as_str())) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("module name {} is used twice (module names must be unique across nested gram projects)", duplicate.name),
            ));
        }
        Ok(project)
    }

    fn read(file: &Path) -> io::Result<Self> {
        let contents = fs::read_to_string(file)
            .map_err(|err| io::Error::new(err.kind(), format!("couldn't read {}: {err}", file.display())))?;
        ron::de::from_str(&contents)
            .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, format!("{}: {err}", file.display())))
    }

    /// Writes the root Gram.ron and every nested project's, each with only its own modules and
    /// their `src` relative to that file again.
    pub(crate) fn write(&self) -> io::Result<()> {
        self.write_to(Path::new("."))
    }

    fn write_to(&self, root: &Path) -> io::Result<()> {
        let file = |dir: &str, name: &str, version: &str, main: &str| BuildConfig {
            name: name.to_owned(),
            version: version.to_owned(),
            main: main.to_owned(),
            modules: self.modules.iter()
                .filter(|m| m.project == dir)
                .map(|m| ModuleConfig {
                    src: slash_path(Path::new(&m.src).strip_prefix(dir).expect("a module's src is inside its project")),
                    ..m.clone()
                })
                .collect(),
            subprojects: vec![],
        };

        Directory::new(root)?.write_file("Gram.ron", &file("", &self.name, &self.version, &self.main))?;
        for sub in &self.subprojects {
            Directory::new(root.join(&sub.dir))?.write_file("Gram.ron", &file(&sub.dir, &sub.name, &sub.version, &sub.main))?;
        }
        Ok(())
    }

    pub fn rm_module(&mut self, name: &str) -> io::Result<ModuleConfig> {
        match self.modules.iter().position(|module| module.name == name) {
            result => match result {
                Some(index) => Ok(self.modules.remove(index)),
                None => Err(io::Error::new(io::ErrorKind::Other, "module not found")),
            },
        }
    }

    /// File name (without platform prefix/extension) of `module`'s artifact under build/: the
    /// name of its project if it's that project's main runtime module (the game executable is
    /// "Game", not "Runtime"), the module's own name otherwise.
    pub(crate) fn artifact_name<'a>(&'a self, module: &'a ModuleConfig) -> &'a str {
        if module.kind != "runtime" || module.lang == "gram" {
            return &module.name;
        }
        let (name, main) = if module.project.is_empty() {
            (self.name.as_str(), self.main.as_str())
        } else {
            match self.subprojects.iter().find(|sub| sub.dir == module.project) {
                Some(sub) => (sub.name.as_str(), sub.main.as_str()),
                None => return &module.name,
            }
        };
        if main == module.name { name } else { &module.name }
    }

    /// Adds a module to the root project or, with `parent` (the name of a `gram` module), to
    /// that nested project.
    pub fn add_module(&mut self, name: &str, lang: &str, kind: &str, targets: &[String], parent: &str) -> io::Result<()> {
        let project = if parent.is_empty() {
            String::new()
        } else {
            let parent_module = self.modules.iter().find(|m| m.name == parent).ok_or_else(|| io::Error::new(
                io::ErrorKind::NotFound,
                format!("--parent module not found: {parent}"),
            ))?;
            if parent_module.lang != "gram" {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!("--parent {parent} is a \"{}\" module, not a gram module", parent_module.lang),
                ));
            }
            slash_path(parent_module.dir())
        };

        match (kind, lang) {
            ("runtime" | "library", _) | ("dynamic", "cargo") => {}
            ("dynamic", _) => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "\"dynamic\" (a library loaded at runtime) is only supported for cargo modules (a cdylib)",
            )),
            _ => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unknown module kind \"{kind}\": expected \"runtime\", \"library\" or \"dynamic\""),
            )),
        }

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
            src: slash_path(&Path::new(&project).join(name).join(manifest)),
            deps: vec![],
            targets,
            project,
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
            .run(self, target, &command.args)
    }

    pub fn build(&self, ci: &bool, target: &str, module: &str) -> io::Result<()> {
        if *ci && !target.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "--target can't be combined with --ci — --ci already builds every target declared in a module's targets",
            ));
        }
        if !target.is_empty() {
            validate_targets(std::slice::from_ref(&target.to_string()))?;
        }

        let modules: Vec<&ModuleConfig> = match module {
            "" => self.modules.iter().collect(),
            module => {
                let selected = self.modules.iter()
                    .find(|m| m.name == module)
                    .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, format!("module not found: {module}")))?;
                match selected.lang.as_str() {
                    // A nested project: everything inside it, nested projects included.
                    "gram" => {
                        let dir = slash_path(selected.dir());
                        self.modules.iter().filter(|m| Path::new(&m.project).starts_with(&dir)).collect()
                    }
                    _ => vec![selected],
                }
            }
        };

        let name = self.name.clone();
        println!("gram build (name={name}, ci={ci})");
        for curr_module in modules {
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
            self.add_module(&module, &lang, &command.kind, &targets, &command.parent)?;
            self.modules.last().unwrap().save(&project)?;
            self.write()?;
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
                self.write()?;
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
            if name == module {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "a module can't reference itself"));
            }

            let resolved = self.resolve(&module, &name)?;
            if resolved.kind != "library" {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!("referenced module {name} is a \"{}\" module, not a library — only library modules can be referenced", resolved.kind),
                ));
            }
            if consumer.lang != resolved.lang {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    format!(
                        "cross-language module references aren't supported yet ({module} is \"{}\", {name} is \"{}\")",
                        consumer.lang, resolved.lang
                    ),
                ));
            }

            // `resolved` owns its data (no borrow into self.modules), so the consumer can be
            // borrowed mutably here.
            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("checked above")
                .add_module_ref(&resolved)?;
        } else {
            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("module was just created above, or already existed")
                .add_dependency(&dependency)?;
        }

        self.write()?;
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
            return self.write();
        }

        let removed = self.rm_module(&module)?;
        if self.main == module {
            self.main = String::new();
        }
        if removed.lang == "gram" {
            // Its modules (and any project nested in it) go with it, from Gram.ron at least.
            let dir = slash_path(removed.dir());
            let inside = |project: &str| Path::new(project).starts_with(&dir);
            self.modules.retain(|m| !inside(&m.project));
            self.subprojects.retain(|sub| !inside(&sub.dir));
        }
        self.write()?;
        self.write_vscode_settings(&project)?;

        if command.purge {
            let module_dir = Path::new(&removed.src).parent().unwrap_or_else(|| Path::new("."));
            // Two modules can share a directory (a CMakeLists.txt and a Cargo.toml side by side):
            // purging one must not delete the other's sources.
            if let Some(other) = self.modules.iter().find(|m| m.dir().starts_with(module_dir)) {
                println!(
                    "gram module (name={module}) removed; {} kept, module {} still uses it",
                    module_dir.display(), other.name
                );
                return Ok(());
            }
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
        self.write_to(Path::new(path))?;
        println!("gram init (name={name}, path={path})");
        for curr_module in &self.modules {
            curr_module.save(&project)?;
        }
        self.write_vscode_settings(&project)
    }
}
