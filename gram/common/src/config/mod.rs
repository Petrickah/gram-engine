use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use serde::{Deserialize, Serialize};

use crate::commands::add::AddCommand;
use crate::commands::remove::RemoveCommand;
use crate::commands::run::RunCommand;
use crate::directory::Directory;
use crate::lang::backend_for;
use crate::target::validate_targets;

pub mod module;

use module::ModuleConfig;

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

    pub fn add_dependency(&mut self, command: AddCommand) -> io::Result<()> {
        let project    = Directory::new(".")?;
        let lang          = command.lang;
        let targets  = command.target;
        let module        = command.module;
        let dependency    = command.name;

        if !self.modules.iter().any(|m| m.name == module) {
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
                existing.targets.extend(added.into_iter().cloned());
                println!("gram module (name={module}): added target(s) {added_display}");
                project.write_file("Gram.ron", self)?;
            }
        }

        if !dependency.is_empty() {
            if command.module_ref {
                if !command.url.is_empty() || !command.tag.is_empty() || command.vcpkg {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "--module-ref can't be combined with --url/--tag/--vcpkg (those are for external dependencies)",
                    ));
                }
                if dependency == module {
                    return Err(io::Error::new(io::ErrorKind::InvalidInput, "a module can't reference itself"));
                }

                let dep = self.modules.iter().find(|m| m.name == dependency).ok_or_else(|| io::Error::new(
                    io::ErrorKind::NotFound,
                    format!("referenced module not found: {dependency} (create it first, e.g. `gram add --module {dependency} --lang ...`)"),
                ))?;
                if dep.kind != "library" {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("referenced module {dependency} is a \"{}\" module, not a library — only library modules can be referenced", dep.kind),
                    ));
                }
                let dep_lang = dep.lang.clone();

                let consumer = self.modules.iter().find(|m| m.name == module).expect("module was just created above, or already existed");
                if consumer.lang != dep_lang {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!(
                            "cross-language module references aren't supported yet ({module} is \"{}\", {dependency} is \"{dep_lang}\")",
                            consumer.lang
                        ),
                    ));
                }

                self.modules.iter_mut()
                    .find(|m| m.name == module)
                    .expect("checked above")
                    .add_module_ref(&dependency)?;
                return project.write_file("Gram.ron", self);
            }

            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("module was just created above, or already existed")
                .add_dependency(&dependency, &command.url, &command.tag, command.vcpkg)?;
            return project.write_file("Gram.ron", self);
        }

        Ok(())
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
