use std::io;

pub(crate) const SUPPORTED_TARGETS: [&str; 3] = ["linux", "windows", "macos"];

/// Whether `target` can be built from the host gram is running on: natively for the
/// host's own OS, or Windows from anywhere via the mingw cross toolchain. There's no
/// cross toolchain between linux and macos (either direction), so those only build natively.
pub(crate) fn buildable_on_host(target: &str) -> bool {
    target == std::env::consts::OS || target == "windows"
}

/// The `--target` triple cargo needs for `target`, or None for a native host build
/// (plain `cargo build`, output in target/debug). Only Windows is cross-compiled.
pub(crate) fn cargo_cross_triple(target: &str) -> io::Result<Option<&'static str>> {
    if target == std::env::consts::OS {
        return Ok(None);
    }
    match target {
        "windows" => Ok(Some("x86_64-pc-windows-gnu")),
        other => Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("can't build target {other} from a {} host", std::env::consts::OS),
        )),
    }
}

pub(crate) fn validate_targets(targets: &[String]) -> io::Result<()> {
    for target in targets {
        if !SUPPORTED_TARGETS.contains(&target.as_str()) {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported target: {target} (supported: {})", SUPPORTED_TARGETS.join(", ")),
            ));
        }
    }
    Ok(())
}
