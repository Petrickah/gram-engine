use std::io;
use clap::Args;
use crate::BuildConfig;
use crate::config::AddOutcome;

#[derive(Args)]
pub struct AddCommand {
    /// The name of the dependency we want to add. For a cmake module this is the
    /// FetchContent name; with --module-ref, the name of another module already
    /// in this project. Omit to only create --module.
    #[arg(default_value = "")]
    pub(crate) name: String,
    /// Path for the project we want to add a dependency or module
    #[arg(short, long, default_value = "")]
    pub(crate) path: String,
    /// The module to add the dependency to, created first if it doesn't exist
    #[arg(short, long, default_value = "main")]
    pub(crate) module: String,
    /// Create --module inside this gram module's project instead of the root one. Ignored if --module already exists.
    #[arg(long, default_value = "")]
    pub(crate) parent: String,
    /// Language for --module if it needs to be created: "cargo", "cmake" or "gram" (a nested Gram project). Ignored if --module already exists.
    #[arg(long, default_value = "cargo")]
    pub(crate) lang: String,
    /// Kind for --module if it needs to be created: "runtime" or "library". Ignored if --module already exists or --lang isn't cmake.
    #[arg(long, default_value = "runtime")]
    pub(crate) kind: String,
    /// Git repository URL for the dependency (cmake FetchContent's GIT_REPOSITORY). Required when adding a dependency to a cmake module.
    #[arg(long, default_value = "")]
    pub(crate) url: String,
    /// Git tag/branch/commit for the dependency (cmake FetchContent's GIT_TAG). Optional; omitted uses the repo's default branch.
    #[arg(long, default_value = "")]
    pub(crate) tag: String,
    /// Resolve the dependency via a vcpkg manifest (vcpkg.json) instead of FetchContent. Only valid for a cmake module; can't be combined with --url/--tag.
    #[arg(long)]
    pub(crate) vcpkg: bool,
    /// Targets --module supports, comma-separated: "linux", "windows", "macos". If --module is being created, sets its initial target list (omit to default to just the host OS); if --module already exists, appends any of these not already declared.
    #[arg(short, long, value_delimiter = ',')]
    pub(crate) target: Vec<String>,
    /// Treat the dependency name as another module in this same project instead of an external one: a cargo path dependency, or (cmake) add_subdirectory + target_link_libraries. Both modules must already exist, be the same language, and the referenced one must be a "library". Can't be combined with --url/--tag/--vcpkg.
    #[arg(long)]
    pub(crate) module_ref: bool,
}

pub fn module(command: AddCommand) -> io::Result<AddOutcome> {
    let path = &command.path;

    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }

    BuildConfig::open(".")?.add_dependency(command)
}