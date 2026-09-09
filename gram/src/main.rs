use clap::{Parser, Subcommand};

mod add;
mod build;
mod common;
mod init;

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
        /// Should new module be Git tracked
        #[arg(long)]
        git: bool,
    },
    /// Build the current project by reading gram-build.ron.
    Build {
        /// Path for the project we want to build
        #[arg(short, long, default_value = "")]
        path: String,
        /// Produce artifacts in the CI-standard layout instead of the local one.
        #[arg(long)]
        ci: bool,
    },
    /// Add a module and/or a dependency to a project. Creates --module if it
    /// doesn't exist yet; omit the dependency name to just create the module.
    Add {
        /// The name of the dependency we want to add. Omit to only create --module.
        #[arg(default_value = "")]
        name: String,
        /// Path for the project we want to add a dependency or module
        #[arg(short, long, default_value = "")]
        path: String,
        /// The module to add the dependency to, created first if it doesn't exist
        #[arg(short, long, default_value = "main")]
        module: String,
    }
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Init { name, lang , git} => {
            match init::project(&name, &lang, &git) {
                Ok(_) => println!("Project created successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        },
        Commands::Build { path, ci } => {
            match build::project(&path, &ci) {
                Ok(_) => println!("Project built successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        },
        Commands::Add { name, path, module } => {
            match add::module(&name, &path, &module) {
                Ok(_) => println!("Module dependency added successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        }
    }
}
