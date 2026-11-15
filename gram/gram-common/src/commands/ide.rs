use std::io;
use std::path::PathBuf;
use clap::Args;
use crate::BuildConfig;
use crate::config::ide::Ide;

#[derive(Args)]
pub struct IdeCommand {
    /// The IDE: "xcode" (macOS) or "vs" (Visual Studio, Windows).
    ide: Ide,
    /// Path for the project we want an IDE project for
    #[arg(short, long, default_value = "")]
    path: String,
    /// Open the generated project in the IDE.
    #[arg(long)]
    open: bool,
    /// Only write build/ide/<ide>/CMakeLists.txt, without configuring it (no IDE needed).
    #[arg(long)]
    generate_only: bool,
}

pub fn project(command: IdeCommand) -> io::Result<PathBuf> {
    if !command.path.is_empty() {
        std::env::set_current_dir(&command.path)?;
    }
    BuildConfig::open(".")?.ide(command.ide, command.generate_only, command.open)
}
