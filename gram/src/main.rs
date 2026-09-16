use clap::{Parser, Subcommand};

mod add;
mod rm;
mod build;
mod common;
mod init;
mod run;

/// Gram — build tool CLI for the Gram game engine.
#[derive(Parser)]
#[command(name = "gram", version, about, long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Create a new Gram project (gram-build.ron + a Hello World module).
    Init {
        name: String,
        /// Language of the initial module: "cargo" or "cmake".
        #[arg(short, long, default_value = "cargo")]
        lang: String,
        /// Kind of the initial module: "runtime" (executable) or "library" (dynamic). Only meaningful for --lang cmake.
        #[arg(short, long, default_value = "runtime")]
        kind: String,
        /// Should new module be Git tracked
        #[arg(long)]
        git: bool,
        /// Targets the initial module supports, comma-separated: "linux", "windows" (more later). Omit to default to just the host OS.
        #[arg(short, long, value_delimiter = ',')]
        target: Vec<String>,
    },
    /// Build the current project by reading gram-build.ron.
    Build {
        /// Path for the project we want to build
        #[arg(short, long, default_value = "")]
        path: String,
        /// Produce artifacts in the CI-standard layout instead of the local one.
        #[arg(long)]
        ci: bool,
        /// Cross-build for a single target ("linux"/"windows") instead of the host OS. Can't be combined with --ci, which already builds every target declared in targets.
        #[arg(short, long, default_value = "")]
        target: String,
    },
    /// Add a module and/or a dependency to a project. Creates --module if it
    /// doesn't exist yet; omit the dependency name to just create the module.
    Add {
        /// The name of the dependency we want to add. For a cmake module this is the
        /// FetchContent name; with --module-ref, the name of another module already
        /// in this project. Omit to only create --module.
        #[arg(default_value = "")]
        name: String,
        /// Path for the project we want to add a dependency or module
        #[arg(short, long, default_value = "")]
        path: String,
        /// The module to add the dependency to, created first if it doesn't exist
        #[arg(short, long, default_value = "main")]
        module: String,
        /// Language for --module if it needs to be created: "cargo" or "cmake". Ignored if --module already exists.
        #[arg(long, default_value = "cargo")]
        lang: String,
        /// Kind for --module if it needs to be created: "runtime" or "library". Ignored if --module already exists or --lang isn't cmake.
        #[arg(long, default_value = "runtime")]
        kind: String,
        /// Git repository URL for the dependency (cmake FetchContent's GIT_REPOSITORY). Required when adding a dependency to a cmake module.
        #[arg(long, default_value = "")]
        url: String,
        /// Git tag/branch/commit for the dependency (cmake FetchContent's GIT_TAG). Optional; omitted uses the repo's default branch.
        #[arg(long, default_value = "")]
        tag: String,
        /// Resolve the dependency via a vcpkg manifest (vcpkg.json) instead of FetchContent. Only valid for a cmake module; can't be combined with --url/--tag.
        #[arg(long)]
        vcpkg: bool,
        /// Targets --module supports, comma-separated: "linux", "windows" (more later). If --module is being created, sets its initial target list (omit to default to just the host OS); if --module already exists, appends any of these not already declared.
        #[arg(short, long, value_delimiter = ',')]
        target: Vec<String>,
        /// Treat the dependency name as another module in this same project instead of an external one: a cargo path dependency, or (cmake) add_subdirectory + target_link_libraries. Both modules must already exist, be the same language, and the referenced one must be a "library". Can't be combined with --url/--tag/--vcpkg.
        #[arg(long)]
        module_ref: bool,
    },
    /// Remove a module and/or a dependency to a project. Removes --module if it
    /// exists; omit the dependency name to just remove the module.
    Remove {
        /// The name of the dependency we want to remove. Omit to only remove --module.
        #[arg(default_value = "")]
        name: String,
        /// Path for the project we want to remove a dependency or module
        #[arg(short, long, default_value = "")]
        path: String,
        /// The module to remove the dependency from, removed first if no dependency was specified
        #[arg(short, long, default_value = "main")]
        module: String,
        /// Also delete --module's directory from disk. Only applies when removing a whole module (no dependency name given); ignored otherwise.
        #[arg(long)]
        purge: bool,
    },
    /// Run an already-built runtime module's binary from its build directory.
    Run {
        /// Module to run. Omit to run the project's main module (set by `gram init`).
        #[arg(short, long, default_value = "")]
        module: String,
        /// Path for the project we want to run
        #[arg(short, long, default_value = "")]
        path: String,
        /// Build target to run ("linux"/"windows"). Omit to use the host OS.
        #[arg(short, long, default_value = "")]
        target: String,
        /// Arguments passed through to the running binary. Put `--` before them, e.g. `gram run -- --help`.
        #[arg(last = true)]
        args: Vec<String>,
    }
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Init { name, lang, kind, git, target } => {
            match init::project(&name, &lang, &kind, &git, &target) {
                Ok(_) => println!("Project created successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        },
        Commands::Build { path, ci, target } => {
            match build::project(&path, &ci, &target) {
                Ok(_) => println!("Project built successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        },
        Commands::Add { name, path, module, lang, kind, url, tag, vcpkg, target, module_ref } => {
            match add::module(&name, &path, &module, &lang, &kind, &url, &tag, &vcpkg, &target, &module_ref) {
                Ok(_) => println!("Module dependency added successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        },
        Commands::Remove { name, path, module, purge } => {
            match rm::module(&name, &path, &module, &purge) {
                Ok(_) => println!("Module dependency removed successfully."),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        },
        Commands::Run { module, path, target, args } => {
            match run::module(&module, &path, &target, &args) {
                Ok(code) => std::process::exit(code),
                Err(err) => {
                    eprintln!("Error: {err}");
                    std::process::exit(1)
                }
            }
        }
    }
}
