use std::io;

use crate::common::BuildConfig;

pub fn module(module: &str, path: &str, target: &str, args: &[String]) -> io::Result<i32> {
    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }
    BuildConfig::open(".")?.run(module, target, args)
}
