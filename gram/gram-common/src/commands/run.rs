use std::io;
use clap::Args;
use crate::BuildConfig;
use crate::profile::Profile;

#[derive(Args)]
pub struct RunCommand {
    /// Module to run. Omit to run the project's main module (set by `gram init`).
    #[arg(short, long, default_value = "")]
    pub(crate) module: String,
    /// Path for the project we want to run
    #[arg(short, long, default_value = "")]
    pub(crate) path: String,
    /// Build target to run ("linux"/"windows"/"macos"). Omit to use the host OS.
    #[arg(short, long, default_value = "")]
    pub(crate) target: String,
    /// Build configuration to run ("debug"/"release"/"final"): the one `gram build --config` built.
    #[arg(short, long, default_value = "debug")]
    pub(crate) config: Profile,
    /// Arguments passed through to the running binary. Put `--` before them, e.g. `gram run -- --help`.
    #[arg(last = true)]
    pub(crate) args: Vec<String>,
}

pub fn module(command: RunCommand) -> io::Result<i32> {
    let path = &command.path;
    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }

    BuildConfig::open(".")?.run(command)
}
