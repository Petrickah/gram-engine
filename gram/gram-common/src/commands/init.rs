use std::io;
use std::process::Command;
use clap::Args;
use crate::BuildConfig;

#[derive(Args)]
pub struct InitCommand
{
    name: String,
    /// Language of the initial module: "cargo" or "cmake".
    #[arg(short, long, default_value = "cargo")]
    lang: String,
    /// Kind of the initial module: "runtime" (executable) or "library" (dynamic). Only meaningful for --lang cmake.
    #[arg(short, long, default_value = "runtime")]
    kind: String,
    /// Should new module be Git tracked
    #[arg(long)]
    git: bool,
    /// Targets the initial module supports, comma-separated: "linux", "windows", "macos". Omit to default to just the host OS.
    #[arg(short, long, value_delimiter = ',')]
    target: Vec<String>,
}

pub fn project(command: InitCommand) -> io::Result<()> {
    let name: &str = &command.name;
    let mut project = BuildConfig::new(name, "0.1.0")?;
    project.add_module("main", &command.lang, &command.kind, &command.target.to_vec())?;
    project.set_main("main");
    project.save(&format!("./{name}/").to_string())?;

    if command.git {
        let status = Command::new("git")
            .args(["init", &format!("./{name}/")])
            .status()?;
        if !status.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "git init failed"));
        }
    }

    Ok(())
}