use std::fs;
use std::io;
use std::path::Path;
use std::process::Command;
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
    /// Start the game over ssh, not through the device's Steam shortcut (when Devices.ron gives it one).
    #[arg(long)]
    pub(crate) direct: bool,
    /// Start the game under lldb-server, stopped until a debugger connects: VS Code's "Game on device", or lldb with
    /// build/deploy/debug.lldb (written here).
    #[arg(long)]
    pub(crate) debug: bool,
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
    if command.debug && device.os == deploy::DeviceOs::Windows {
        return Err(io::Error::new(io::ErrorKind::Unsupported, format!(
            "--debug isn't supported on a Windows device yet ({}): lldb-server is for Linux; a mingw build would want gdbserver",
            device.name,
        )));
    }

    let options = DeployOptions {
        config: command.config,
        module: command.module,
        executable: project.main_executable_path(device.os.target(), command.config)?,
        run: !command.no_run,
        // gram-engine carries gram's own sources: the device builds that gram, the one that knows this tree's Gram.ron.
        gram_manifest: Path::new("gram/Cargo.toml").exists().then(|| "gram/Cargo.toml".to_owned()),
        direct: command.direct,
        debug: command.debug,
        local_gram: std::env::current_exe()?.display().to_string(),
    };

    if options.debug && options.run && !command.dry_run {
        write_lldb_commands(device, &options.executable)?;
    }
    deploy::execute(&deploy::plan(device, &options), command.dry_run)
}

/// `build/deploy/debug.lldb`, for the debugger here: the paths on the device need its home, asked over ssh.
fn write_lldb_commands(device: &deploy::Device, executable: &str) -> io::Result<()> {
    let output = Command::new("ssh").args([device.host.as_str(), "printf %s \"$HOME\""]).output()?;
    let home = String::from_utf8_lossy(&output.stdout).trim().to_owned();
    if !output.status.success() || home.is_empty() {
        return Err(io::Error::other(format!("couldn't read the home folder of {} over ssh", device.name)));
    }
    let local_root = std::env::current_dir()?.display().to_string();
    let path = Path::new("build/deploy/debug.lldb");
    fs::create_dir_all(path.parent().expect("a file in a folder"))?;
    fs::write(path, deploy::lldb_commands(device, &home, executable, &local_root))?;
    println!("gram deploy: wrote {} (the device's paths for lldb)", path.display());
    Ok(())
}
