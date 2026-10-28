use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

pub struct Directory {
    pub(crate) path: PathBuf
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

    pub fn read_source(&self, relative_path: &str) -> io::Result<Option<String>> {
        match fs::read_to_string(self.path.join(relative_path)) {
            Ok(contents) => Ok(Some(contents)),
            Err(err) if err.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(err) => Err(err),
        }
    }
}
