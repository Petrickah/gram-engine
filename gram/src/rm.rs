use std::io;

use crate::common::BuildConfig;

pub fn module(name: &str, path: &str, module: &str, purge: &bool) -> io::Result<()> {
    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }
    BuildConfig::open(".")?.rm_dependency(module, name, purge)
}