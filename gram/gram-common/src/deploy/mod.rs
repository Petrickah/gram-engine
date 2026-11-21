//! `gram deploy`: build and run the project on another machine over SSH (a Steam Deck, the Legion Go S with
//! SteamOS, any Linux box), the way it was first done by hand (`docs/gram-native-toolchains.md`, *SteamOS*).
//!
//! macOS can't build for Linux, so the sources go to the device and the device builds them: `rsync` the tree,
//! build `gram` there, `gram build`, then start the game on the device's screen with its output streamed back.
//!
//! - `device` — `Devices.ron`, the git-ignored list of this machine's devices (written by Claude).
//! - `plan` — what to run for a device, as a list of commands; the commands themselves (written by the user).
//! - `execute` — runs the plan's steps one after another, or prints them (`--dry-run`).

pub mod device;
pub mod plan;

use std::io;
use std::process::Command;

pub use device::{Device, Devices, DEVICES_FILE};
pub use plan::{DeployOptions, Step, plan, quote};

/// Runs every step in order, each one's output going straight to this terminal. Stops at the first step that
/// fails. Returns the last step's exit code: the game's, when the plan ends by running it.
///
/// With `dry_run`, only prints each step's command line, as it could be pasted into a shell.
pub fn execute(steps: &[Step], dry_run: bool) -> io::Result<i32> {
    let mut code = 0;
    for (index, step) in steps.iter().enumerate() {
        println!("gram deploy [{}/{}]: {}", index + 1, steps.len(), step.what);
        if dry_run {
            println!("  {}", step.command_line());
            continue;
        }

        let status = Command::new(&step.program).args(&step.args).status().map_err(|err| {
            io::Error::new(err.kind(), format!("couldn't start {} ({}): {err}", step.program, step.what))
        })?;
        code = status.code().unwrap_or(1);

        let last = index + 1 == steps.len();
        if !status.success() && !last {
            return Err(io::Error::other(format!("{} failed (exit code {code}): {}", step.what, step.command_line())));
        }
    }
    Ok(code)
}
