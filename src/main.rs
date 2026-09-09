use clap::{Parser, Subcommand};

mod init;
mod build;
mod common;

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
    Init {
        name: String,
        /// Language of the initial module (determines the source file's extension).
        #[arg(short, long, default_value = "cpp")]
        lang: String,
    },
    /// Build the current project by reading gram-build.ron.
    Build {
        #[arg(short, long, default_value = "")]
        path: String,
        /// Produce artifacts in the CI-standard layout instead of the local one.
        #[arg(long)]
        ci: bool,
    },
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Init { name, lang } => {
            match init::project(&name, &lang) {
                Ok(_) => println!("Project created successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        }
        Commands::Build { path, ci } => {
            match build::project(&path, &ci) {
                Ok(_) => println!("Project built successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        }
    }
}
