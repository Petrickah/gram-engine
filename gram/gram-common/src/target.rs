use std::io;

pub(crate) const SUPPORTED_TARGETS: [&str; 3] = ["linux", "windows", "macos"];

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
