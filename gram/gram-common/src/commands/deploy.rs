use std::io;
use std::path::Path;
use clap::Args;
use crate::BuildConfig;
use crate::deploy::{self, DeployOptions, Devices};
use crate::profile::Profile;

#[derive(Args)]
pub struct DeployCommand {
    /// Device to deploy to, by name, from Devices.ron next to Gram.ron. Omit for its default (or its only) device.
    #[arg(short, long)]
    pub(crate) device: Option<String>,
    /// Build configuration built and run on the device ("debug"/"release"/"final").
    #[arg(short, long, default_value = "debug")]
    pub(crate) config: Profile,
    /// Build only this module on the device (like `gram build --module`). Omit to build the whole tree.
    #[arg(short, long)]
    pub(crate) module: Option<String>,
    /// Copy and build only; don't start the game.
    #[arg(long)]
    pub(crate) no_run: bool,
    /// Print the commands instead of running them.
    #[arg(long)]
    pub(crate) dry_run: bool,
    /// Path of the project to deploy.
    #[arg(short, long, default_value = "")]
    pub(crate) path: String,
}

/// Returns the game's exit code when it ran, else 0.
pub fn project(command: DeployCommand) -> io::Result<i32> {
    if !command.path.is_empty() {
        std::env::set_current_dir(&command.path)?;
    }

    let project = BuildConfig::open(".")?;
    let devices = Devices::open(Path::new("."))?;
    let device = devices.find(command.device.as_deref())?;

    let options = DeployOptions {
        config: command.config,
        module: command.module,
        executable: project.main_executable_path("linux", command.config)?,
        run: !command.no_run,
        // gram-engine carries gram's own sources: the device builds that gram, the one that knows this tree's Gram.ron.
        gram_manifest: Path::new("gram/Cargo.toml").exists().then(|| "gram/Cargo.toml".to_owned()),
    };

    deploy::execute(&deploy::plan(device, &options), command.dry_run)
}
