use std::io;

use crate::common::BuildConfig;

pub fn project(path: &str, ci: &bool) -> io::Result<()> {
    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }
    BuildConfig::open(".")?.build(ci)
}