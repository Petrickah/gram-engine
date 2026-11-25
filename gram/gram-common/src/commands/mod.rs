use clap::Parser;
use clap::Subcommand;
use crate::commands::add::AddCommand;
use crate::commands::benchmark::BenchmarkCommand;
use crate::commands::build::BuildCommand;
use crate::commands::deploy::DeployCommand;
use crate::commands::generate::GenerateCommand;
use crate::commands::ide::IdeCommand;
use crate::commands::init::InitCommand;
use crate::commands::remove::RemoveCommand;
use crate::commands::run::RunCommand;

pub mod add;
pub mod benchmark;
pub mod build;
pub mod deploy;
pub mod generate;
pub mod ide;
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
    Run(RunCommand),
    /// Generate the component code (components.generated.rs, Components.generated.hpp) of
    /// each cargo module from the .ron schemas in its directory.
    Generate(GenerateCommand),
    /// Generate one IDE project (Xcode or Visual Studio) for the whole tree: the cmake modules,
    /// plus a target per Rust dynamic module that runs `gram build --module` on it.
    Ide(IdeCommand),
    /// Copy the project to a device listed in Devices.ron (a Linux machine reachable over SSH), build it there
    /// and start the game on its screen, its output streamed back here.
    Deploy(DeployCommand),
    /// Graph benchmark recordings (the Profiler panel's Record button) with tools/plot_benchmark.py: by default
    /// every one in the benchmarks/ folder of the game built for --config (release) and --target.
    Benchmark(BenchmarkCommand),
}