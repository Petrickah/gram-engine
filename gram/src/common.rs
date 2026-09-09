use std::fs;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::process::Command;

use serde::{Deserialize, Serialize};

pub struct Directory {
    path: PathBuf
}

#[derive(Debug, Serialize, Deserialize)]
pub struct BuildConfig {
    name: String,
    version: String,
    modules: Vec<ModuleConfig>
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ModuleConfig {
    name: String,
    lang: String,
    src: String,
    deps: Vec<String>,
    targets: Vec<String>
}

impl BuildConfig {
    pub fn new(name: &str, version: &str) -> io::Result<Self> {
        Ok(Self {
            name: String::from(name),
            version: String::from(version),
            modules: vec![]
        })
    }

    pub fn open(path: &str) -> io::Result<Self> {
        let path = PathBuf::new().join(path);
        let mut file = fs::File::open(path.join("gram-build.ron"))?;
        let mut contents = String::new();
        file.read_to_string(&mut contents)?;

        match ron::de::from_bytes(&contents.as_bytes()) {
            Ok(project) => Ok(project),
            Err(err) => Err(io::Error::new(io::ErrorKind::InvalidData, err)),
        }
    }

    pub fn add_module(&mut self, name: &str, lang: &str) -> io::Result<()> {
        match lang {
            "cargo" => Ok(self.modules.push(ModuleConfig { 
                name: String::from(name),
                lang: String::from("cargo"),
                src: format!("{name}/Cargo.toml"),
                deps: vec![],
                targets: vec![String::from(std::env::consts::OS)]
            })),
            other => Ok(self.modules.push(ModuleConfig {
                name: String::from(name),
                lang: String::from(other),
                src: format!("src/{name}.{other}"),
                deps: vec![],
                targets: vec![String::from(std::env::consts::OS)]
            })),
        }
    }

    pub fn build(&self, ci: &bool) -> io::Result<()> {
        let name = self.name.clone();
        println!("gram build (name={name}, ci={ci})");
        for curr_module in &self.modules {
            curr_module.build(&self, &ci)?;
        }
        Ok(())
    }

    pub fn add_dependency(&mut self, module: &str, dependency: &str) -> io::Result<()> {
        let project = Directory::new(".")?;

        if !self.modules.iter().any(|m| m.name == module) {
            self.add_module(module, "cargo")?;
            self.modules.last().unwrap().save(&project)?;
            // Persist right away: if the dependency step below fails, the
            // module itself (already materialized on disk) stays tracked.
            project.write_file("gram-build.ron", self)?;
            self.write_vscode_settings(&project)?;
            println!("gram module (name={module}, lang=cargo) created");
        }

        if !dependency.is_empty() {
            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("module was just created above, or already existed")
                .add_dependency(dependency)?;
            project.write_file("gram-build.ron", self)?;
        }

        Ok(())
    }

    pub fn save(&mut self, path: &str) -> io::Result<()> {
        let name = self.name.clone();
        let project = Directory::new(path)?;
        project.write_file("gram-build.ron", self)?;
        println!("gram init (name={name}, path={path})");
        for curr_module in &self.modules {
            curr_module.save(&project)?;
        }
        self.write_vscode_settings(&project)
    }

    fn write_vscode_settings(&self, project: &Directory) -> io::Result<()> {
        let manifests: Vec<&str> = self.modules.iter()
            .filter(|m| m.lang == "cargo")
            .map(|m| m.src.as_str())
            .collect();

        if manifests.is_empty() {
            return Ok(());
        }

        let entries = manifests.iter()
            .map(|path| format!("        \"{path}\""))
            .collect::<Vec<_>>()
            .join(",\n");

        let content = format!(
            "{{\n    \"rust-analyzer.linkedProjects\": [\n{entries}\n    ]\n}}\n"
        );

        project.write_source(".vscode/settings.json", &content)
    }
}

impl ModuleConfig {
    pub fn add_dependency(&mut self, dependency: &str) -> io::Result<()> {
        match self.lang.as_str() {
            "cargo" => self.add_dependency_cargo(dependency),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("adding dependencies isn't supported yet for module language: {other}"),
            )),
        }
    }

    fn add_dependency_cargo(&mut self, dependency: &str) -> io::Result<()> {
        println!("gram add (dependency={dependency}, src={}): adding the new dependency", self.src);
        let mut command = Command::new("cargo");
        command.args(["add", dependency, "--manifest-path", &self.src.clone()]);
        
        let status = command.status()?;
        match status.success() {
            true => Ok(self.deps.push(dependency.to_owned())),
            false => Err(io::Error::new(io::ErrorKind::Other, "cargo add failed"))
        }
    }

    pub fn build(&self, project: &BuildConfig, ci: &bool) -> io::Result<()> {
        let name = self.name.clone();
        let lang = self.lang.clone();
        println!("gram module (name={name}, lang={lang}, ci={ci})");

        match ci {
            true => {
                for target in &self.targets {
                    self.build_target(project, ci, target)?;
                }
                Ok(())
            },
            false => self.build_target(project, ci, std::env::consts::OS),
        }
    }

    fn build_target(&self, project: &BuildConfig, ci: &bool, target: &str) -> io::Result<()> {
        let name = self.name.clone();
        println!("gram target (name={name}, target={target})");

        let builds = match ci {
            true => {
                let version = project.version.clone();
                let project = project.name.clone();
                format!("builds/{project}_{version}_{target}")
            },
            false => format!("builds/{target}"),
        };

        fs::create_dir_all(&builds)?;
        let ext = if target == "windows" { ".exe" } else { "" };
        let out = format!("{builds}/{name}{ext}");

        match self.lang.as_str() {
            "cpp" => self.compile_cpp(&out, target),
            "rs" => self.compile_rust(&out, target),
            "cargo" => self.compile_cargo(&out, target),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported module language: {other}"),
            )),
        }
    }

    fn compile_cargo(&self, out: &str, target: &str) -> io::Result<()> {
        let name = self.name.clone();
        let manifest_dir = Path::new(&self.src).parent().unwrap_or_else(|| Path::new("."));
        let mut command = Command::new("cargo");
        command.args(["build", "--manifest-path", &self.src.clone()]);

        let from = match target {
            "linux" => manifest_dir.join("target/debug").join(&name),
            "windows" => {
                command.args(["--target", "x86_64-pc-windows-gnu"]);
                manifest_dir.join("target/x86_64-pc-windows-gnu/debug").join(format!("{name}.exe"))
            },
            other => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported target: {other}"),
            )),
        };

        let status = command.status()?;
        match status.success() {
            true => std::fs::rename(from, out),
            false => Err(io::Error::new(io::ErrorKind::Other, "cargo build failed"))
        }
    }

    fn compile_rust(&self, out: &str, target: &str) -> io::Result<()> {
        let mut command = Command::new("rustc");
        command.args([self.src.to_string(), "-o".to_string(), out.to_string()]);

        match target {
            "linux" => {},
            "windows" => { command.args(["--target", "x86_64-pc-windows-gnu"]); },
            other => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported target: {other}"),
            )),
        }

        let status = command.status()?;
        if !status.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "rustc failed"));
        }

        Ok(())
    }

    fn compile_cpp(&self, out: &str, target: &str) -> io::Result<()> {
        let (compiler, extra_args): (&str, &[&str]) = match target {
            "linux" => ("g++", &[]),
            // static runtime, otherwise the binary depends on libstdc++-6.dll/libgcc_s at runtime.
            "windows" => ("x86_64-w64-mingw32-g++", &["-static-libstdc++", "-static-libgcc"]),
            other => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported target: {other}"),
            )),
        };

        let status = Command::new(compiler)
            .args([self.src.as_str(), "-o", out])
            .args(extra_args)
            .status()?;

        if !status.success() {
            return Err(io::Error::new(io::ErrorKind::Other, format!("{compiler} failed")));
        }

        Ok(())
    }

    fn save_cpp(&self, project: &Directory) -> Result<(), std::io::Error> {
        let content = "#include <iostream>\n\nint main() {\n    std::cout << \"Hello, World!\" << std::endl;\n    return 0;\n}\n";
        project.write_source(&self.src, content)
    }

    fn save_rust(&self, project: &Directory) -> Result<(), std::io::Error> {
        let content = "fn main() {\n    println!(\"Hello, World!\");\n}\n";
        project.write_source(&self.src, content)
    }

    fn save_cargo(&self, project: &Directory) -> Result<(), std::io::Error> {
        let manifest_dir = project.path.join(
            Path::new(&self.src).parent().unwrap_or_else(|| Path::new("."))
        );

        match fs::create_dir_all(&manifest_dir) {
            Ok(_) => {
                let mut command = Command::new("cargo");
                command.args(["init", "--name", &self.name, "--vcs", "none", "--edition", "2024"]);
                command.current_dir(&manifest_dir);

                let status = command.status()?;
                match status.success() {
                    true => Ok(()),
                    false => Err(io::Error::new(io::ErrorKind::Other, "cargo init failed"))
                }
            },
            Err(err) => Err(io::Error::new(io::ErrorKind::AlreadyExists, format!("Error creating module: {err}"))),
        }
    }

    pub fn save(&self, project: &Directory) -> Result<(), std::io::Error> {
        let name = self.name.clone();
        let lang = self.lang.clone();
        println!("gram module (name={name}, lang={lang})");
        match self.lang.as_str() {
            "cpp" => self.save_cpp(project),
            "rs" => self.save_rust(project),
            "cargo" => self.save_cargo(project),
            other => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported module language: {other}"),
            )),
        }
    }
}

impl Directory {
    pub fn new<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let path = path.as_ref().to_path_buf();
        fs::create_dir_all(&path)?;
        Ok(Self { path: path })
    }

    pub fn write_file<Config: serde::Serialize>(&self, filename: &str, config: &Config) -> Result<(), std::io::Error> {
        let pretty_config = ron::ser::PrettyConfig::default().struct_names(true);
        match ron::ser::to_string_pretty(&config, pretty_config) {
            Ok(config) => {
                let mut file = fs::File::create(self.path.join(filename))?;
                file.write_all(config.as_bytes())
            }
            Err(error) => Err(io::Error::new(io::ErrorKind::InvalidData, error)),
        }
    }

    pub fn write_source(&self, relative_path: &str, content: &str) -> Result<(), std::io::Error> {
        let full_path = self.path.join(relative_path);
        if let Some(parent) = full_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut file = fs::File::create(full_path)?;
        file.write_all(content.as_bytes())
    }
}