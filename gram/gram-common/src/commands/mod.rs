use clap::Parser;
use clap::Subcommand;
use crate::commands::add::AddCommand;
use crate::commands::build::BuildCommand;
use crate::commands::init::InitCommand;
use crate::commands::remove::RemoveCommand;
use crate::commands::run::RunCommand;

pub mod add;
pub mod build;
pub mod init;
pub mod remove;
pub mod run;

/// Gram — build tool CLI for the Gram game engine.
#[derive(Parser)]
#[command(name = "gram", version, about, long_about = None)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand)]
pub enum Commands {
    /// Create a new Gram project (gram-build.ron + a Hello World module).
    Init(InitCommand),
    /// Build the current project by reading gram-build.ron.
    Build(BuildCommand),
    /// Add a module and/or a dependency to a project. Creates --module if it
    /// doesn't exist yet; omit the dependency name to just create the module.
    Add(AddCommand),
    /// Remove a module and/or a dependency to a project. Removes --module if it
    /// exists; omit the dependency name to just remove the module.
    Remove(RemoveCommand),
    /// Run an already-built runtime module's binary from its build directory.
    Run(RunCommand)
}