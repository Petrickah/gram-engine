use std::io;

use crate::common::BuildConfig;

pub fn project(name: &str, lang: &str) -> io::Result<()> {
    let mut project = BuildConfig::new(name, "0.1.0")?;
    project.add_module("main", lang)?;
    project.save(&format!("./{name}/").to_string())
}
