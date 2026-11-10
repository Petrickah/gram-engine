//! vcpkg manifest (vcpkg.json) handling for cmake modules. Not a module language of its own:
//! the CMake backend uses these when a dependency is resolved through vcpkg instead of
//! FetchContent.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde_json::{json, Map, Value};

use crate::config::module::ModuleConfig;
use crate::lang::DependencySpec;
use crate::lang::ResolvedModule;
use crate::lang::cmake::DEPS_END;

/// Which vcpkg manifest a cmake module's configure should use.
pub(crate) enum BuildManifest {
    /// The module's own vcpkg.json, which vcpkg finds on its own (top-level source dir).
    Own,
    /// A manifest merged from the module's and its referenced modules' vcpkg.json files,
    /// written to this directory; pass it as VCPKG_MANIFEST_DIR.
    Merged(PathBuf),
}

/// vcpkg's manifest mode only reads the vcpkg.json of the *top-level* CMake project, so a module
/// pulled in with add_subdirectory (a module ref) would get its vcpkg dependencies silently
/// skipped. When referenced modules have manifests, their dependencies are merged with the
/// module's own into `<build dir>/vcpkg-manifest/vcpkg.json`. None: no vcpkg involved at all.
///
/// Only direct references are followed (a referenced module's own refs aren't, yet).
pub(crate) fn build_manifest(module: &ModuleConfig, refs: &[ResolvedModule], build_dir: &Path) -> io::Result<Option<BuildManifest>> {
    let own = module.dir().join("vcpkg.json");
    // `dir` is relative to the project root, which is gram's cwd.
    let referenced: Vec<PathBuf> = refs.iter()
        .map(|resolved| resolved.dir.join("vcpkg.json"))
        .filter(|path| path.exists())
        .collect();

    if referenced.is_empty() {
        return Ok(own.exists().then_some(BuildManifest::Own));
    }

    let manifests: Vec<PathBuf> = own.exists().then_some(own).into_iter().chain(referenced).collect();
    let mut merged = Map::new();
    let mut dependencies: Vec<Value> = Vec::new();
    for path in &manifests {
        let malformed = |detail: String| io::Error::new(io::ErrorKind::InvalidData, format!("malformed {}: {detail}", path.display()));
        let manifest: Value = serde_json::from_str(&fs::read_to_string(path)?).map_err(|err| malformed(err.to_string()))?;
        let Some(fields) = manifest.as_object() else { return Err(malformed(String::from("not a JSON object"))) };

        for (key, field) in fields {
            match key.as_str() {
                "dependencies" => for dependency in field.as_array().into_iter().flatten() {
                    match dependencies.iter_mut().find(|known| dependency_name(known) == dependency_name(dependency)) {
                        Some(known) => merge_dependency(known, dependency),
                        None => dependencies.push(dependency.clone()),
                    }
                },
                // Describe the merged manifest itself, set below.
                "name" | "version" => {},
                // builtin-baseline, overrides, ...: the first manifest that sets one wins.
                _ => { merged.entry(key.clone()).or_insert_with(|| field.clone()); },
            }
        }
    }
    merged.insert(String::from("name"), json!(sanitize_name(&module.name)));
    merged.insert(String::from("version"), json!("0.1.0"));
    merged.insert(String::from("dependencies"), Value::Array(dependencies));

    let dir = std::env::current_dir()?.join(build_dir).join("vcpkg-manifest");
    fs::create_dir_all(&dir)?;
    let pretty = serde_json::to_string_pretty(&Value::Object(merged))
        .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;
    fs::write(dir.join("vcpkg.json"), format!("{pretty}\n"))?;
    Ok(Some(BuildManifest::Merged(dir)))
}

/// A vcpkg dependency is either a plain port name or an object with a "name" field.
fn dependency_name(dependency: &Value) -> Option<&str> {
    dependency.as_str().or_else(|| dependency.get("name")?.as_str())
}

/// The same port requested by two manifests: keep the richer form and the union of features.
/// Differing "platform" filters aren't reconciled; the first manifest's wins.
fn merge_dependency(known: &mut Value, other: &Value) {
    if known.is_string() {
        if other.is_object() {
            *known = other.clone();
        }
        return;
    }
    let (Some(known), Some(other)) = (known.as_object_mut(), other.as_object()) else { return };
    let Some(other_features) = other.get("features").and_then(Value::as_array) else { return };
    if let Some(features) = known.entry("features").or_insert_with(|| json!([])).as_array_mut() {
        for feature in other_features {
            if !features.contains(feature) {
                features.push(feature.clone());
            }
        }
    }
}

/// Adds a dependency resolved via a vcpkg manifest (vcpkg.json) instead of FetchContent:
/// records it in the module's vcpkg.json and inserts a plain `find_package(...)` in the
/// managed CMakeLists.txt region. Like the FetchContent path, wiring `target_link_libraries`
/// is left to you — the right target name varies per vcpkg port and isn't something gram can
/// reliably guess.
pub(crate) fn add_dependency(module: &mut ModuleConfig, dependency: &DependencySpec) -> io::Result<()> {
    let name = dependency.name();
    println!("gram add (dependency={name}, src={}): adding vcpkg dependency", module.src);

    let manifest_path = manifest_path(module);
    let mut manifest = if manifest_path.exists() { read_manifest(&manifest_path)? } else { Map::new() };
    let dependencies = dependencies_mut(&mut manifest, &manifest_path)?;
    if !dependencies.iter().any(|known| dependency_name(known) == Some(name.as_str())) {
        dependencies.push(json!(name));
        write_manifest(&manifest_path, &sanitize_name(&module.name), manifest)?;
    }

    let contents = fs::read_to_string(&module.src)?;
    let begin_marker = format!("# gram:dep:{name} begin");

    if !contents.contains(&begin_marker) {
        let block = format!(
            "{begin_marker}\nfind_package({name} CONFIG REQUIRED)\n# gram:dep:{name} end\n"
        );
        let contents = contents.replacen(DEPS_END, &format!("{block}{DEPS_END}"), 1);
        fs::write(&module.src, contents)?;
    }

    module.deps.push(dependency.clone());
    Ok(())
}

/// Drops `dependency` from the module's vcpkg.json, if the module has one and lists it.
pub(crate) fn remove_dependency(module: &ModuleConfig, dependency: &str) -> io::Result<()> {
    let manifest_path = manifest_path(module);
    if manifest_path.exists() {
        let mut manifest = read_manifest(&manifest_path)?;
        let dependencies = dependencies_mut(&mut manifest, &manifest_path)?;
        let before = dependencies.len();
        dependencies.retain(|known| dependency_name(known) != Some(dependency));
        if dependencies.len() != before {
            write_manifest(&manifest_path, &sanitize_name(&module.name), manifest)?;
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

/// The whole manifest, so a rewrite keeps every field (builtin-baseline, overrides, ...) and every
/// dependency as written: a port can be a plain name or an object with a platform filter or features.
fn read_manifest(path: &Path) -> io::Result<Map<String, Value>> {
    let malformed = |detail: String| io::Error::new(io::ErrorKind::InvalidData, format!("malformed {}: {detail}", path.display()));
    match serde_json::from_str(&fs::read_to_string(path)?).map_err(|err| malformed(err.to_string()))? {
        Value::Object(fields) => Ok(fields),
        _ => Err(malformed(String::from("not a JSON object"))),
    }
}

fn dependencies_mut<'a>(manifest: &'a mut Map<String, Value>, path: &Path) -> io::Result<&'a mut Vec<Value>> {
    manifest.entry("dependencies").or_insert_with(|| json!([])).as_array_mut()
        .ok_or_else(|| io::Error::new(io::ErrorKind::InvalidData, format!("malformed {}: \"dependencies\" isn't an array", path.display())))
}

/// Written by hand rather than pretty-printed: one dependency per line, an object kept on its line
/// (`{ "name": "vulkan", "platform": "windows" }`), as these manifests are written when edited by hand.
/// "name" and "version" come first (filled in when missing), the other fields in their original order.
fn write_manifest(path: &Path, name: &str, mut manifest: Map<String, Value>) -> io::Result<()> {
    let name = manifest.shift_remove("name").unwrap_or_else(|| json!(name));
    let version = manifest.shift_remove("version").unwrap_or_else(|| json!("0.1.0"));
    let fields = [(String::from("name"), name), (String::from("version"), version)].into_iter().chain(manifest);

    let lines: Vec<String> = fields.map(|(key, value)| {
        let value = match &value {
            Value::Array(entries) if key == "dependencies" && !entries.is_empty() => {
                let entries: Vec<String> = entries.iter().map(|entry| format!("        {}", inline(entry))).collect();
                format!("[\n{}\n    ]", entries.join(",\n"))
            },
            _ => inline(&value),
        };
        format!("    {}: {value}", Value::String(key))
    }).collect();
    fs::write(path, format!("{{\n{}\n}}\n", lines.join(",\n")))
}

/// A JSON value on one line, spaced like the hand-written manifests.
fn inline(value: &Value) -> String {
    match value {
        Value::Object(fields) if fields.is_empty() => String::from("{}"),
        Value::Object(fields) => {
            let fields: Vec<String> = fields.iter().map(|(key, value)| format!("{}: {}", Value::String(key.clone()), inline(value))).collect();
            format!("{{ {} }}", fields.join(", "))
        },
        Value::Array(entries) => format!("[{}]", entries.iter().map(inline).collect::<Vec<_>>().join(", ")),
        other => other.to_string(),
    }
}
