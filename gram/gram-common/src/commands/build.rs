use std::io;
use clap::Args;
use crate::BuildConfig;

#[derive(Args)]
pub struct BuildCommand {
    /// Path for the project we want to build
    #[arg(short, long, default_value = "")]
    path: String,
    /// Produce artifacts in the CI-standard layout instead of the local one.
    #[arg(long)]
    ci: bool,
    /// Cross-build for a single target ("linux"/"windows"/"macos") instead of the host OS — only the host OS itself or "windows" (via mingw) can be built. Can't be combined with --ci, which already builds every target declared in targets.
    #[arg(short, long, default_value = "")]
    target: String,
    /// Build only this module instead of the whole tree; for a gram module (a nested project), every module inside it. What it references is still compiled, by cargo or cmake themselves.
    #[arg(short, long, default_value = "")]
    module: String,
}

pub fn project(command: BuildCommand) -> io::Result<()> {
    let path = command.path;
    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }
    BuildConfig::open(".")?.build(&command.ci, &command.target, &command.module)
}