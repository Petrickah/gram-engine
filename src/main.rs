use clap::{Parser, Subcommand};

/// Gram — build tool CLI for the Gram game engine.
#[derive(Parser)]
#[command(name = "gram", version, about, long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Create a new Gram project (gram-build.ron + a Hello World source file).
    Init { name: String },
    /// Build the current project by reading gram-build.ron.
    Build {
        /// Produce artifacts in the CI-standard layout instead of the local one.
        #[arg(long)]
        ci: bool,
    },
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Init { name } => {
            todo!("gram init {name}: not yet implemented (Task 0.2)");
        }
        Commands::Build { ci } => {
            todo!("gram build (ci={ci}): not yet implemented (Task 0.3/0.4)");
        }
    }
}
