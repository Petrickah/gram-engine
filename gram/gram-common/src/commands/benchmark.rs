use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;
use clap::Args;
use crate::BuildConfig;
use crate::profile::Profile;

/// Where the plotting script lives, relative to the project root.
pub const SCRIPT: &str = "tools/plot_benchmark.py";

#[derive(Args)]
pub struct BenchmarkCommand {
    /// Recordings (.csv) or folders of them. Omit for the `benchmarks/` folder next to the game built for --config
    /// and --target: what the Profiler panel's Record button wrote there.
    pub(crate) paths: Vec<String>,
    /// Build configuration whose recordings to graph ("debug"/"release"/"final"): Release, where benchmarks are recorded.
    #[arg(short, long, default_value = "release")]
    pub(crate) config: Profile,
    /// Build target whose recordings to graph ("linux"/"windows"/"macos"). Omit for the host OS.
    #[arg(short, long, default_value = "")]
    pub(crate) target: String,
    /// Rows dropped at the start of each recording (the warm-up).
    #[arg(long, default_value_t = 1)]
    pub(crate) skip: u32,
    /// Start the FPS axis at 0.
    #[arg(long)]
    pub(crate) zero: bool,
    /// Where to save the comparison of several recordings (default: comparison.png next to the first one).
    #[arg(long)]
    pub(crate) output: Option<String>,
    /// Also open the graphs in a window.
    #[arg(long)]
    pub(crate) show: bool,
    /// Path of the project.
    #[arg(short, long, default_value = "")]
    pub(crate) path: String,
}

/// The Python to run the script with: the project's own environment (`.venv`, where matplotlib was installed) when
/// there is one, else the one on the `PATH`.
pub fn python(project_root: &Path, windows: bool) -> PathBuf {
    let venv = if windows { project_root.join(".venv").join("Scripts").join("python.exe") }
               else       { project_root.join(".venv").join("bin").join("python") };
    if venv.is_file() { venv } else { PathBuf::from(if windows { "python" } else { "python3" }) }
}

/// The script's arguments: the paths, then the options set. Pure, so it's tested without Python.
pub fn script_args(paths: &[String], skip: u32, zero: bool, output: Option<&str>, show: bool) -> Vec<String> {
    let mut args = vec![SCRIPT.to_string()];
    args.extend(paths.iter().cloned());
    args.extend(["--skip".to_string(), skip.to_string()]);
    if zero { args.push("--zero".into()); }
    if let Some(output) = output { args.extend(["--output".to_string(), output.to_string()]); }
    if show { args.push("--show".into()); }
    args
}

/// Runs tools/plot_benchmark.py on the recordings; returns its exit code.
pub fn plot(command: BenchmarkCommand) -> io::Result<i32> {
    if !command.path.is_empty() {
        std::env::set_current_dir(&command.path)?;
    }
    if !Path::new(SCRIPT).is_file() {
        return Err(io::Error::new(io::ErrorKind::NotFound, format!("{SCRIPT} not found in this project")));
    }

    let paths = if command.paths.is_empty() {
        // The game's folder for this configuration and target, as `gram run` finds it.
        let target = if command.target.is_empty() { std::env::consts::OS } else { &command.target };
        let executable = BuildConfig::open(".")?.main_executable_path(target, command.config)?;
        let folder = Path::new(&executable).parent().unwrap_or(Path::new(".")).join("benchmarks");
        if !folder.is_dir() {
            return Err(io::Error::new(io::ErrorKind::NotFound, format!(
                "no recordings in {} — record some with the Profiler panel's Record button in that build, or pass --config/--target",
                folder.display()
            )));
        }
        vec![folder.to_string_lossy().into_owned()]
    } else {
        command.paths.clone()
    };

    let python = python(Path::new("."), cfg!(windows));
    let args = script_args(&paths, command.skip, command.zero, command.output.as_deref(), command.show);
    let status = Command::new(&python).args(&args).status().map_err(|err| io::Error::new(err.kind(), format!(
        "couldn't start {}: {err} — install Python 3, or create .venv with matplotlib in the project root",
        python.display()
    )))?;
    Ok(status.code().unwrap_or(1))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn passes_the_paths_then_the_options() {
        let args = script_args(&["build/benchmarks".into()], 1, true, Some("out.png"), false);
        assert_eq!(args, ["tools/plot_benchmark.py", "build/benchmarks", "--skip", "1", "--zero", "--output", "out.png"]);
    }

    #[test]
    fn leaves_out_the_options_not_set() {
        let args = script_args(&["a.csv".into(), "b.csv".into()], 0, false, None, true);
        assert_eq!(args, ["tools/plot_benchmark.py", "a.csv", "b.csv", "--skip", "0", "--show"]);
    }

    #[test]
    fn falls_back_to_the_path_python_without_a_venv() {
        let root = std::env::temp_dir().join("gram-benchmark-no-venv");
        assert_eq!(python(&root, false), PathBuf::from("python3"));
        assert_eq!(python(&root, true),  PathBuf::from("python"));
    }
}
