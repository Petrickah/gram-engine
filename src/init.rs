use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

struct Directory {
    path: PathBuf
}

#[derive(Debug, Serialize, Deserialize)]
struct BuildConfig {
    name: String,
    version: String,
    modules: Vec<ModuleConfig>
}

#[derive(Debug, Serialize, Deserialize)]
struct ModuleConfig {
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

    pub fn add_module(&mut self, name: &str, lang: &str) -> Result<(), io::Error> {
        Ok(self.modules.push(ModuleConfig {
            name: String::from(name),
            lang: String::from(lang),
            src: format!("src/{name}.{lang}"),
            deps: vec![],
            targets: vec![]
        }))
    }

    pub fn save(&mut self, path: &str) -> Result<(), std::io::Error> {
        let project = Directory::new(path)?;
        project.write_file("gram-build.ron", self)?;
        for curr_module in &self.modules {
            curr_module.save(&project)?;
        }
        Ok(())
    }
}

impl ModuleConfig {
    pub fn save(&self, project: &Directory) -> Result<(), std::io::Error> {
        let content = match self.lang.as_str() {
            "cpp" => "#include <iostream>\n\nint main() {\n    std::cout << \"Hello, World!\" << std::endl;\n    return 0;\n}\n",
            "rs" => "fn main() {\n    println!(\"Hello, World!\");\n}\n",
            other => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported module language: {other}"),
            )),
        };
        project.write_source(&self.src, content)
    }
}

impl Directory {
    pub fn new<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let path = path.as_ref().to_path_buf();
        fs::create_dir_all(&path)?;
        Ok(Self { path: path })
    }

    pub fn write_file<Config: serde::Serialize>(&self, filename: &str, config: &Config) -> Result<(), std::io::Error> {
        match ron::ser::to_string_pretty(&config, ron::ser::PrettyConfig::default()) {
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

pub fn project(name: &str, lang: &str) -> Result<(), std::io::Error> {
    let mut project = BuildConfig::new(name, "0.1.0")?;
    project.add_module("main", lang)?;
    project.save(&format!("./{name}/").to_string())
}
