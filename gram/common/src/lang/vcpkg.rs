//! vcpkg manifest (vcpkg.json) handling for cmake modules. Not a module language of its own:
//! the CMake backend uses these when a dependency is resolved through vcpkg instead of
//! FetchContent.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::config::module::ModuleConfig;
use crate::lang::cmake::DEPS_END;

/// Adds a dependency resolved via a vcpkg manifest (vcpkg.json) instead of FetchContent:
/// records it in the module's vcpkg.json and inserts a plain `find_package(...)` in the
/// managed CMakeLists.txt region. Like the FetchContent path, wiring `target_link_libraries`
/// is left to you — the right target name varies per vcpkg port and isn't something gram can
/// reliably guess.
pub(crate) fn add_dependency(module: &mut ModuleConfig, dependency: &str) -> io::Result<()> {
    println!("gram add (dependency={dependency}, src={}): adding vcpkg dependency", module.src);

    let manifest_path = manifest_path(module);
    let mut manifest_deps = if manifest_path.exists() {
        read_dependencies(&manifest_path)?
    } else {
        vec![]
    };

    if !manifest_deps.iter().any(|d| d == dependency) {
        manifest_deps.push(dependency.to_owned());
        write_manifest(&manifest_path, &sanitize_name(&module.name), &manifest_deps)?;
    }

    let contents = fs::read_to_string(&module.src)?;
    let begin_marker = format!("# gram:dep:{dependency} begin");

    if !contents.contains(&begin_marker) {
        let block = format!(
            "{begin_marker}\nfind_package({dependency} CONFIG REQUIRED)\n# gram:dep:{dependency} end\n"
        );
        let contents = contents.replacen(DEPS_END, &format!("{block}{DEPS_END}"), 1);
        fs::write(&module.src, contents)?;
    }

    module.deps.push(dependency.to_owned());
    Ok(())
}

/// Drops `dependency` from the module's vcpkg.json, if the module has one and lists it.
pub(crate) fn remove_dependency(module: &ModuleConfig, dependency: &str) -> io::Result<()> {
    let manifest_path = manifest_path(module);
    if manifest_path.exists() {
        let mut manifest_deps = read_dependencies(&manifest_path)?;
        if let Some(position) = manifest_deps.iter().position(|d| d == dependency) {
            manifest_deps.remove(position);
            write_manifest(&manifest_path, &sanitize_name(&module.name), &manifest_deps)?;
        }
    }
    Ok(())
}

fn manifest_path(module: &ModuleConfig) -> PathBuf {
    let module_dir = Path::new(&module.src).parent().unwrap_or_else(|| Path::new("."));
    module_dir.join("vcpkg.json")
}

/// vcpkg manifest names must be lowercase alphanumeric + hyphens; this is only used
/// for vcpkg.json's own "name" metadata field, not for resolving ports.
fn sanitize_name(name: &str) -> String {
    let lower: String = name.chars()
        .map(|c| if c.is_ascii_alphanumeric() { c.to_ascii_lowercase() } else { '-' })
        .collect();
    let collapsed = lower.split('-').filter(|s| !s.is_empty()).collect::<Vec<_>>().join("-");
    if collapsed.is_empty() { String::from("module") } else { collapsed }
}

fn read_dependencies(path: &Path) -> io::Result<Vec<String>> {
    let contents = fs::read_to_string(path)?;
    let start = contents.find("\"dependencies\"").and_then(|i| contents[i..].find('[').map(|j| i + j + 1));
    let end = start.and_then(|s| contents[s..].find(']').map(|e| s + e));

    match (start, end) {
        (Some(s), Some(e)) => Ok(contents[s..e]
            .split(',')
            .map(|entry| entry.trim().trim_matches('"').to_string())
            .filter(|entry| !entry.is_empty())
            .collect()),
        _ => Err(io::Error::new(io::ErrorKind::InvalidData, format!("malformed vcpkg.json: {}", path.display()))),
    }
}

fn write_manifest(path: &Path, name: &str, deps: &[String]) -> io::Result<()> {
    let dependencies = if deps.is_empty() {
        String::from("[]")
    } else {
        let entries = deps.iter().map(|d| format!("        \"{d}\"")).collect::<Vec<_>>().join(",\n");
        format!("[\n{entries}\n    ]")
    };

    let content = format!(
        "{{\n    \"name\": \"{name}\",\n    \"version\": \"0.1.0\",\n    \"dependencies\": {dependencies}\n}}\n"
    );
    fs::write(path, content)
}
