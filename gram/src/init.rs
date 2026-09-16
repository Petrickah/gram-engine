use std::io;
use std::process::Command;

use crate::common::BuildConfig;

pub fn project(name: &str, lang: &str, kind: &str, git: &bool, targets: &[String]) -> io::Result<()> {
    let mut project = BuildConfig::new(name, "0.1.0")?;
    project.add_module("main", lang, kind, targets)?;
    project.set_main("main");
    project.save(&format!("./{name}/").to_string())?;

    if *git {
        let status = Command::new("git")
            .args(["init", &format!("./{name}/")])
            .status()?;
        if !status.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "git init failed"));
        }
    }

    Ok(())
}
