use std::io;
use clap::Args;
use crate::BuildConfig;
use crate::config::Generation;

#[derive(Args)]
pub struct GenerateCommand {
    /// Path for the project to generate code for
    #[arg(short, long, default_value = "")]
    path: String,
    /// Only this cargo module (by default every cargo module of the tree)
    #[arg(short, long, default_value = "")]
    module: String,
}

pub fn project(command: GenerateCommand) -> io::Result<Vec<Generation>> {
    if !command.path.is_empty() {
        std::env::set_current_dir(&command.path)?;
    }
    BuildConfig::open(".")?.generate(&command.module)
}
