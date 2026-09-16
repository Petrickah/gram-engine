use std::io;

use crate::common::BuildConfig;

pub fn module(name: &str, path: &str, module: &str, lang: &str, kind: &str, url: &str, tag: &str, vcpkg: &bool, targets: &[String], module_ref: &bool) -> io::Result<()> {
    if !path.is_empty() {
        std::env::set_current_dir(path)?;
    }
    BuildConfig::open(".")?.add_dependency(module, name, lang, kind, url, tag, vcpkg, targets, module_ref)
}