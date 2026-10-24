use std::io;
use clap::Args;
use crate::BuildConfig;

#[derive(Args)]
pub struct RemoveCommand {
    /// The name of the dependency we want to remove. Omit to only remove --module.
    #[arg(default_value = "")]
    pub(crate) name: String,
    /// Path for the project we want to remove a dependency or module
    #[arg(short, long, default_value = "")]
    pub(crate) path: String,
    /// The module to remove the dependency from, removed first if no dependency was specified
    #[arg(short, long, default_value = "main")]
    pub(crate) module: String,
    /// Also delete --module's directory from disk. Only applies when removing a whole module (no dependency name given); ignored otherwise.
    #[arg(long)]
    pub(crate) purge: bool,
}

pub fn module(command: RemoveCommand) -> io::Result<()> {
    let path = &command.path;

    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }

    BuildConfig::open(".")?.rm_dependency(command)
}