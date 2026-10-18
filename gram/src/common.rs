use std::fs;
use std::io::{self, Read, Write};
use std::path::{Path, PathBuf};
use std::process::Command;

use serde::{Deserialize, Serialize};

pub struct Directory {
    path: PathBuf
}

// Marker lines bracketing the block of gram-managed FetchContent dependencies
// inside a cmake module's CMakeLists.txt, so add/remove can edit that region
// as text without parsing the rest of the file.
const DEPS_BEGIN: &str = "# --- gram dependencies (managed, do not edit by hand) ---";
const DEPS_END: &str = "# --- end gram dependencies ---";

fn default_kind() -> String {
    String::from("runtime")
}

const SUPPORTED_TARGETS: [&str; 3] = ["linux", "windows", "macos"];

/// Whether `target` can be built from the host gram is running on: natively for the
/// host's own OS, or Windows from anywhere via the mingw cross toolchain. There's no
/// cross toolchain between linux and macos (either direction), so those only build natively.
fn buildable_on_host(target: &str) -> bool {
    target == std::env::consts::OS || target == "windows"
}

/// The `--target` triple cargo needs for `target`, or None for a native host build
/// (plain `cargo build`, output in target/debug). Only Windows is cross-compiled.
fn cargo_cross_triple(target: &str) -> io::Result<Option<&'static str>> {
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

fn validate_targets(targets: &[String]) -> io::Result<()> {
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

#[derive(Debug, Serialize, Deserialize)]
pub struct BuildConfig {
    name: String,
    version: String,
    /// Name of the project's main runtime module, set once by `gram init`. `gram run`
    /// falls back to this when --module is omitted. Empty (the default for a
    /// Gram.ron predating this field) means there's no implicit default.
    #[serde(default)]
    main: String,
    modules: Vec<ModuleConfig>
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ModuleConfig {
    name: String,
    lang: String,
    /// "runtime" (executable) or "library" (dynamic). Only meaningful for lang == "cmake".
    #[serde(default = "default_kind")]
    kind: String,
    src: String,
    deps: Vec<String>,
    targets: Vec<String>
}

impl BuildConfig {
    pub fn new(name: &str, version: &str) -> io::Result<Self> {
        Ok(Self {
            name: String::from(name),
            version: String::from(version),
            main: String::new(),
            modules: vec![]
        })
    }

    pub fn set_main(&mut self, module: &str) {
        self.main = String::from(module);
    }

    pub fn open(path: &str) -> io::Result<Self> {
        let path = PathBuf::new().join(path);
        let mut file = fs::File::open(path.join("Gram.ron"))?;
        let mut contents = String::new();
        file.read_to_string(&mut contents)?;

        match ron::de::from_bytes(&contents.as_bytes()) {
            Ok(project) => Ok(project),
            Err(err) => Err(io::Error::new(io::ErrorKind::InvalidData, err)),
        }
    }

    pub fn rm_module(&mut self, name: &str) -> io::Result<ModuleConfig> {
        match self.modules.iter().position(|module| module.name == name) {
            result => match result {
                Some(index) => Ok(self.modules.swap_remove(index)),
                None => Err(io::Error::new(io::ErrorKind::Other, "module not found")),
            },
        }
    }

    pub fn add_module(&mut self, name: &str, lang: &str, kind: &str, targets: &[String]) -> io::Result<()> {
        let targets = if targets.is_empty() {
            vec![String::from(std::env::consts::OS)]
        } else {
            validate_targets(targets)?;
            targets.to_vec()
        };

        match lang {
            "cargo" => Ok(self.modules.push(ModuleConfig {
                name: String::from(name),
                lang: String::from("cargo"),
                kind: String::from(kind),
                src: format!("{name}/Cargo.toml"),
                deps: vec![],
                targets
            })),
            "cmake" => Ok(self.modules.push(ModuleConfig {
                name: String::from(name),
                lang: String::from("cmake"),
                kind: String::from(kind),
                src: format!("{name}/CMakeLists.txt"),
                deps: vec![],
                targets
            })),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported module language: {other}"),
            )),
        }
    }

    pub fn run(&self, module: &str, target: &str, args: &[String]) -> io::Result<i32> {
        let module = if module.is_empty() { self.main.as_str() } else { module };
        if module.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "no --module given, and this project has no main module set (only `gram init` sets one) — pass --module explicitly",
            ));
        }

        let target = if target.is_empty() { std::env::consts::OS } else { target };

        self.modules.iter()
            .find(|m| m.name == module)
            .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, format!("module not found: {module}")))?
            .run(&self.name, target, args)
    }

    pub fn build(&self, ci: &bool, target: &str) -> io::Result<()> {
        if *ci && !target.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "--target can't be combined with --ci — --ci already builds every target declared in a module's targets",
            ));
        }
        if !target.is_empty() {
            validate_targets(std::slice::from_ref(&target.to_string()))?;
        }

        let name = self.name.clone();
        println!("gram build (name={name}, ci={ci})");
        for curr_module in &self.modules {
            curr_module.build(&self, &ci, target)?;
        }
        Ok(())
    }

    pub fn add_dependency(&mut self, module: &str, dependency: &str, lang: &str, kind: &str, url: &str, tag: &str, vcpkg: &bool, targets: &[String], module_ref: &bool) -> io::Result<()> {
        let project = Directory::new(".")?;

        if !self.modules.iter().any(|m| m.name == module) {
            println!("gram module (name={module}, lang={lang}) created");
            self.add_module(module, lang, kind, targets)?;
            self.modules.last().unwrap().save(&project)?;
            project.write_file("Gram.ron", self)?;
            self.write_vscode_settings(&project)?;
        } else if !targets.is_empty() {
            validate_targets(targets)?;
            let existing = self.modules.iter_mut().find(|m| m.name == module).expect("checked above");
            let added: Vec<&String> = targets.iter().filter(|t| !existing.targets.contains(t)).collect();
            if !added.is_empty() {
                let added_display = added.iter().map(|t| t.as_str()).collect::<Vec<_>>().join(", ");
                existing.targets.extend(added.into_iter().cloned());
                println!("gram module (name={module}): added target(s) {added_display}");
                project.write_file("Gram.ron", self)?;
            }
        }

        if !dependency.is_empty() {
            if *module_ref {
                if !url.is_empty() || !tag.is_empty() || *vcpkg {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "--module-ref can't be combined with --url/--tag/--vcpkg (those are for external dependencies)",
                    ));
                }
                if dependency == module {
                    return Err(io::Error::new(io::ErrorKind::InvalidInput, "a module can't reference itself"));
                }

                let dep = self.modules.iter().find(|m| m.name == dependency).ok_or_else(|| io::Error::new(
                    io::ErrorKind::NotFound,
                    format!("referenced module not found: {dependency} (create it first, e.g. `gram add --module {dependency} --lang ...`)"),
                ))?;
                if dep.kind != "library" {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("referenced module {dependency} is a \"{}\" module, not a library — only library modules can be referenced", dep.kind),
                    ));
                }
                let dep_lang = dep.lang.clone();

                let consumer = self.modules.iter().find(|m| m.name == module).expect("module was just created above, or already existed");
                if consumer.lang != dep_lang {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!(
                            "cross-language module references aren't supported yet ({module} is \"{}\", {dependency} is \"{dep_lang}\")",
                            consumer.lang
                        ),
                    ));
                }

                self.modules.iter_mut()
                    .find(|m| m.name == module)
                    .expect("checked above")
                    .add_module_ref(dependency)?;
                return project.write_file("Gram.ron", self);
            }

            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("module was just created above, or already existed")
                .add_dependency(dependency, url, tag, *vcpkg)?;
            return project.write_file("Gram.ron", self);
        }

        Ok(())
    }

    pub fn rm_dependency(&mut self, module: &str, dependency: &str, purge: &bool) -> io::Result<()> {
        let project = Directory::new(".")?;

        if !dependency.is_empty() {
            self.modules.iter_mut()
                .find(|m| m.name == module)
                .expect("module already existed")
                .rm_dependency(dependency)?;
            return project.write_file("Gram.ron", self);
        }

        if self.modules.iter().any(|m| m.name == module) {
            let removed = self.rm_module(module)?;
            if self.main == module {
                self.main = String::new();
            }
            project.write_file("Gram.ron", self)?;
            self.write_vscode_settings(&project)?;

            if *purge {
                let module_dir = Path::new(&removed.src).parent().unwrap_or_else(|| Path::new("."));
                fs::remove_dir_all(module_dir)?;
                println!("gram module (name={module}) removed, {} deleted", module_dir.display());
            } else {
                println!("gram module (name={module}) removed");
            }
        }

        Ok(())
    }

    pub fn save(&mut self, path: &str) -> io::Result<()> {
        let name = self.name.clone();
        let project = Directory::new(path)?;
        project.write_file("Gram.ron", self)?;
        println!("gram init (name={name}, path={path})");
        for curr_module in &self.modules {
            curr_module.save(&project)?;
        }
        self.write_vscode_settings(&project)
    }

    // Merges into .vscode/settings.json rather than overwriting it, so any settings a
    // user (or an IDE extension, e.g. CMake Tools) added by hand survive every add/remove.
    fn write_vscode_settings(&self, project: &Directory) -> io::Result<()> {
        let manifests: Vec<&str> = self.modules.iter()
            .filter(|m| m.lang == "cargo")
            .map(|m| m.src.as_str())
            .collect();

        let existing = project.read_source(".vscode/settings.json")?;
        let mut settings: serde_json::Value = match &existing {
            Some(contents) => serde_json::from_str(contents).unwrap_or(serde_json::json!({})),
            None => serde_json::json!({}),
        };
        let settings_obj = settings.as_object_mut().expect("settings.json root must be an object");

        if manifests.is_empty() {
            settings_obj.remove("rust-analyzer.linkedProjects");
        } else {
            settings_obj.insert(
                "rust-analyzer.linkedProjects".to_string(),
                serde_json::Value::Array(manifests.iter().map(|p| serde_json::Value::String(p.to_string())).collect()),
            );
        }

        if settings_obj.is_empty() {
            return Ok(());
        }

        let pretty = serde_json::to_string_pretty(&settings)
            .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;
        project.write_source(".vscode/settings.json", &format!("{pretty}\n"))
    }
}

impl ModuleConfig {
    pub fn add_dependency(&mut self, dependency: &str, url: &str, tag: &str, vcpkg: bool) -> io::Result<()> {
        match self.lang.as_str() {
            "cargo" => {
                println!("gram add (dependency={dependency}, src={}): adding the new dependency", self.src);
                let mut command = Command::new("cargo");
                command.args(["add", dependency, "--manifest-path", &self.src.clone()]);

                let status = command.status()?;
                match status.success() {
                    true => Ok(self.deps.push(dependency.to_owned())),
                    false => Err(io::Error::new(io::ErrorKind::Other, "cargo add failed"))
                }
            },
            "cmake" => self.add_dependency_cmake(dependency, url, tag, vcpkg),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("adding dependencies isn't supported yet for module language: {other}"),
            )),
        }
    }

    /// Adds another module already in Gram.ron as a dependency (same-language only,
    /// enforced by the caller): a cargo path dependency, or a cmake add_subdirectory +
    /// target_link_libraries. Unlike an external dependency, gram knows the exact target
    /// name here (it generated it), so it can wire the link itself.
    pub fn add_module_ref(&mut self, dependency: &str) -> io::Result<()> {
        match self.lang.as_str() {
            "cargo" => {
                println!("gram add (dependency={dependency}, src={}): adding module reference (cargo path dependency)", self.src);
                let mut command = Command::new("cargo");
                // gram's own module layout is always flat (every module is a direct child
                // of the project root), and gram already runs with cwd = project root, so
                // the dependency module's own directory name is the correct --path as-is.
                command.args(["add", dependency, "--path", dependency, "--manifest-path", &self.src.clone()]);

                let status = command.status()?;
                match status.success() {
                    true => Ok(self.deps.push(dependency.to_owned())),
                    false => Err(io::Error::new(io::ErrorKind::Other, "cargo add (path dependency) failed"))
                }
            },
            "cmake" => self.add_module_ref_cmake(dependency),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("module references aren't supported yet for module language: {other}"),
            )),
        }
    }

    fn add_module_ref_cmake(&mut self, dependency: &str) -> io::Result<()> {
        println!("gram add (dependency={dependency}, src={}): adding module reference (add_subdirectory)", self.src);
        let contents = fs::read_to_string(&self.src)?;
        let begin_marker = format!("# gram:dep:{dependency} begin");

        let contents = if !contents.contains(&begin_marker) {
            let block = format!(
                "{begin_marker}\nadd_subdirectory(${{CMAKE_CURRENT_SOURCE_DIR}}/../{dependency} ${{CMAKE_CURRENT_BINARY_DIR}}/{dependency})\n# gram:dep:{dependency} end\n"
            );
            contents.replacen(DEPS_END, &format!("{block}{DEPS_END}"), 1)
        } else {
            contents
        };

        // target_link_libraries needs the target (add_executable/add_library) to already
        // exist, so it can't live in the DEPS region above — appended after it instead.
        let link_marker = format!("# gram:link:{dependency} begin");
        let contents = if !contents.contains(&link_marker) {
            format!(
                "{contents}\n{link_marker}\ntarget_link_libraries({} PRIVATE {dependency})\n# gram:link:{dependency} end\n",
                self.name
            )
        } else {
            contents
        };

        fs::write(&self.src, contents)?;
        self.deps.push(dependency.to_owned());
        Ok(())
    }

    fn add_dependency_cmake(&mut self, dependency: &str, url: &str, tag: &str, vcpkg: bool) -> io::Result<()> {
        if vcpkg {
            if !url.is_empty() || !tag.is_empty() {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "--vcpkg can't be combined with --url/--tag (those are FetchContent-only)",
                ));
            }
            return self.add_dependency_vcpkg(dependency);
        }

        if url.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "cmake dependencies need --url (used as FetchContent's GIT_REPOSITORY), or pass --vcpkg to resolve from a vcpkg registry instead",
            ));
        }

        println!("gram add (dependency={dependency}, src={}): adding the new dependency", self.src);
        let contents = fs::read_to_string(&self.src)?;
        let begin_marker = format!("# gram:dep:{dependency} begin");

        if !contents.contains(&begin_marker) {
            let tag_line = if tag.is_empty() { String::new() } else { format!("\n  GIT_TAG {tag}") };
            let block = format!(
                "{begin_marker}\nFetchContent_Declare(\n  {dependency}\n  GIT_REPOSITORY {url}{tag_line}\n)\nFetchContent_MakeAvailable({dependency})\n# gram:dep:{dependency} end\n"
            );
            let contents = contents.replacen(DEPS_END, &format!("{block}{DEPS_END}"), 1);
            fs::write(&self.src, contents)?;
        }

        self.deps.push(dependency.to_owned());
        Ok(())
    }

    /// Adds a dependency resolved via a vcpkg manifest (vcpkg.json) instead of FetchContent:
    /// records it in the module's vcpkg.json and inserts a plain `find_package(...)` in the
    /// managed CMakeLists.txt region. Like the FetchContent path, wiring `target_link_libraries`
    /// is left to you — the right target name varies per vcpkg port and isn't something gram can
    /// reliably guess.
    fn add_dependency_vcpkg(&mut self, dependency: &str) -> io::Result<()> {
        println!("gram add (dependency={dependency}, src={}): adding vcpkg dependency", self.src);

        let manifest_path = self.vcpkg_json_path();
        let mut manifest_deps = if manifest_path.exists() {
            Self::read_vcpkg_dependencies(&manifest_path)?
        } else {
            vec![]
        };

        if !manifest_deps.iter().any(|d| d == dependency) {
            manifest_deps.push(dependency.to_owned());
            Self::write_vcpkg_manifest(&manifest_path, &Self::sanitize_vcpkg_name(&self.name), &manifest_deps)?;
        }

        let contents = fs::read_to_string(&self.src)?;
        let begin_marker = format!("# gram:dep:{dependency} begin");

        if !contents.contains(&begin_marker) {
            let block = format!(
                "{begin_marker}\nfind_package({dependency} CONFIG REQUIRED)\n# gram:dep:{dependency} end\n"
            );
            let contents = contents.replacen(DEPS_END, &format!("{block}{DEPS_END}"), 1);
            fs::write(&self.src, contents)?;
        }

        self.deps.push(dependency.to_owned());
        Ok(())
    }

    fn vcpkg_json_path(&self) -> PathBuf {
        let module_dir = Path::new(&self.src).parent().unwrap_or_else(|| Path::new("."));
        module_dir.join("vcpkg.json")
    }

    /// vcpkg manifest names must be lowercase alphanumeric + hyphens; this is only used
    /// for vcpkg.json's own "name" metadata field, not for resolving ports.
    fn sanitize_vcpkg_name(name: &str) -> String {
        let lower: String = name.chars()
            .map(|c| if c.is_ascii_alphanumeric() { c.to_ascii_lowercase() } else { '-' })
            .collect();
        let collapsed = lower.split('-').filter(|s| !s.is_empty()).collect::<Vec<_>>().join("-");
        if collapsed.is_empty() { String::from("module") } else { collapsed }
    }

    fn read_vcpkg_dependencies(path: &Path) -> io::Result<Vec<String>> {
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

    fn write_vcpkg_manifest(path: &Path, name: &str, deps: &[String]) -> io::Result<()> {
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

    pub fn rm_dependency(&mut self, dependency: &str) -> io::Result<String> {
        match self.lang.as_str() {
            "cargo" => {
                println!("gram remove (dependency={dependency}, src={}): removing the dependency", self.src);
                let mut command = Command::new("cargo");
                command.args(["rm", dependency, "--manifest-path", &self.src.clone()]);

                let status = command.status()?;
                match status.success() {
                    true => match self.deps.iter().position(|dep| dep == dependency) {
                        Some(position) => Ok(self.deps.swap_remove(position)),
                        None => Err(io::Error::new(io::ErrorKind::Other, "dependency not found")),
                    },
                    false => Err(io::Error::new(io::ErrorKind::Other, "cargo rm failed"))
                }
            },
            "cmake" => self.rm_dependency_cmake(dependency),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("removing dependencies isn't supported yet for module language: {other}"),
            )),
        }
    }

    fn rm_dependency_cmake(&mut self, dependency: &str) -> io::Result<String> {
        println!("gram remove (dependency={dependency}, src={}): removing the dependency", self.src);
        let contents = fs::read_to_string(&self.src)?;
        let begin_marker = format!("# gram:dep:{dependency} begin");
        let end_marker = format!("# gram:dep:{dependency} end");

        let (start, end) = match (contents.find(&begin_marker), contents.find(&end_marker)) {
            (Some(start), Some(end)) => (start, end + end_marker.len()),
            _ => return Err(io::Error::new(io::ErrorKind::Other, "dependency not found in CMakeLists.txt")),
        };

        let mut new_contents = contents[..start].to_string();
        new_contents.push_str(contents[end..].trim_start_matches('\n'));

        // A module-ref dependency also gets a target_link_libraries block appended after
        // the target (see add_module_ref_cmake) — strip it too if present. Every other
        // dependency kind never has one, so this is a no-op for them.
        let link_begin = format!("# gram:link:{dependency} begin");
        let link_end = format!("# gram:link:{dependency} end");
        if let (Some(start), Some(end)) = (new_contents.find(&link_begin), new_contents.find(&link_end)) {
            let end = end + link_end.len();
            let mut trimmed = new_contents[..start].trim_end_matches('\n').to_string();
            trimmed.push('\n');
            trimmed.push_str(new_contents[end..].trim_start_matches('\n'));
            new_contents = trimmed;
        }

        fs::write(&self.src, new_contents)?;

        let manifest_path = self.vcpkg_json_path();
        if manifest_path.exists() {
            let mut manifest_deps = Self::read_vcpkg_dependencies(&manifest_path)?;
            if let Some(position) = manifest_deps.iter().position(|d| d == dependency) {
                manifest_deps.remove(position);
                Self::write_vcpkg_manifest(&manifest_path, &Self::sanitize_vcpkg_name(&self.name), &manifest_deps)?;
            }
        }

        match self.deps.iter().position(|dep| dep == dependency) {
            Some(position) => Ok(self.deps.swap_remove(position)),
            None => Err(io::Error::new(io::ErrorKind::Other, "dependency not found in module config")),
        }
    }

    pub fn build(&self, project: &BuildConfig, ci: &bool, target: &str) -> io::Result<()> {
        let name = self.name.clone();
        let lang = self.lang.clone();
        println!("gram module (name={name}, lang={lang}, ci={ci})");

        match ci {
            true => {
                for target in &self.targets {
                    if !buildable_on_host(target) {
                        eprintln!(
                            "warning: skipping module {name} target \"{target}\" — can't be built from a {} host",
                            std::env::consts::OS
                        );
                        continue;
                    }
                    self.build_target(project, ci, target)?;
                }
                Ok(())
            },
            false => {
                let target = if target.is_empty() { std::env::consts::OS } else { target };
                if !self.targets.iter().any(|t| t == target) {
                    eprintln!(
                        "warning: module {name} does not declare \"{target}\" as a supported target (declared: {:?})",
                        self.targets
                    );
                }
                if !buildable_on_host(target) {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        format!("can't build target {target} from a {} host", std::env::consts::OS),
                    ));
                }
                self.build_target(project, ci, target)
            },
        }
    }

    fn build_target(&self, project: &BuildConfig, ci: &bool, target: &str) -> io::Result<()> {
        let name = self.name.clone();
        println!("gram target (name={name}, target={target})");
        
        let project_name = project.name.clone();
        let builds = match ci {
            true => {
                let version = project.version.clone();
                format!("build/{project_name}_{version}_{target}")
            },
            false => format!("build/{project_name}/{target}"),
        };

        fs::create_dir_all(&builds)?;
        let (prefix, ext) = self.artifact_naming(target);
        let out = format!("{builds}/{prefix}{name}{ext}");

        match self.lang.as_str() {
            "cargo" => self.compile_cargo(&out, target),
            "cmake" => self.compile_cmake(&out, target),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported module language: {other}"),
            )),
        }
    }

    pub fn run(&self, project: &str, target: &str, args: &[String]) -> io::Result<i32> {
        if self.kind != "runtime" {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("module {} is a \"{}\" module, not runnable (only \"runtime\" modules are)", self.name, self.kind),
            ));
        }

        let (prefix, ext) = self.artifact_naming(target);
        let path = format!("build/{project}/{target}/{prefix}{}{ext}", self.name);

        if !Path::new(&path).exists() {
            return Err(io::Error::new(
                io::ErrorKind::NotFound,
                format!("build artifact not found at {path} — run `gram build` first"),
            ));
        }

        println!("gram run (name={}, target={target})", self.name);
        let status = Command::new(&path).args(args).status()?;
        Ok(status.code().unwrap_or(1))
    }

    /// (filename prefix, extension) for this module's build artifact on `target`.
    /// A cmake "library" module produces a shared library (lib*.so / lib*.dylib / *.dll)
    /// instead of the default executable naming.
    fn artifact_naming(&self, target: &str) -> (&'static str, &'static str) {
        match (self.lang.as_str(), self.kind.as_str(), target) {
            ("cmake", "library", "windows") => ("", ".dll"),
            ("cmake", "library", "macos") => ("lib", ".dylib"),
            ("cmake", "library", _) => ("lib", ".so"),
            (_, _, "windows") => ("", ".exe"),
            _ => ("", ""),
        }
    }

    fn compile_cargo(&self, out: &str, target: &str) -> io::Result<()> {
        let name = self.name.clone();
        let manifest_dir = Path::new(&self.src).parent().unwrap_or_else(|| Path::new("."));
        let mut command = Command::new("cargo");
        command.args(["build", "--manifest-path", &self.src.clone()]);

        if self.kind == "library" {
            // No [lib] crate-type management yet (defaults to rlib) — there's no
            // standalone binary to place under build/, just compile it. Revisit once
            // something (Corrosion, Wave 1) needs a specific crate-type artifact here.
            if let Some(triple) = cargo_cross_triple(target)? {
                command.args(["--target", triple]);
            }
            let status = command.status()?;
            return match status.success() {
                true => Ok(()),
                false => Err(io::Error::new(io::ErrorKind::Other, "cargo build failed")),
            };
        }

        let (_, ext) = self.artifact_naming(target);
        let from = match cargo_cross_triple(target)? {
            None => manifest_dir.join("target/debug").join(format!("{name}{ext}")),
            Some(triple) => {
                command.args(["--target", triple]);
                manifest_dir.join(format!("target/{triple}/debug")).join(format!("{name}{ext}"))
            },
        };

        let status = command.status()?;
        match status.success() {
            true => std::fs::copy(from, out).map(|_| ()),
            false => Err(io::Error::new(io::ErrorKind::Other, "cargo build failed"))
        }
    }

    fn compile_cmake(&self, out: &str, target: &str) -> io::Result<()> {
        let manifest_dir = Path::new(&self.src).parent().unwrap_or_else(|| Path::new("."));
        let build_dir = manifest_dir.join(format!("build-{target}"));
        let build_dir = build_dir.to_str().expect("build dir path is not valid UTF-8");
        let manifest_dir = manifest_dir.to_str().expect("module src path is not valid UTF-8");

        let mut configure = Command::new("cmake");
        // --fresh:  and CMakeLists.txt are regenerated on every add/remove,
        // so reconfigure from scratch each build too — otherwise settings that only take
        // effect on a build dir's *first* configure (CMAKE_TOOLCHAIN_FILE among them) go
        // stale once a module gets vcpkg wired up on top of an already-configured build dir.
        configure.args(["-S", manifest_dir, "-B", build_dir, "--fresh"]);
        // So editors (VS Code's C/C++ extension) can get accurate IntelliSense straight
        // from gram's own build dir, without needing a separate IDE-driven configure.
        configure.arg("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON");

        let has_vcpkg = Path::new(manifest_dir).join("vcpkg.json").exists();
        if has_vcpkg {
            let vcpkg_root = std::env::var("VCPKG_ROOT").map_err(|_| io::Error::new(
                io::ErrorKind::NotFound,
                "module declares vcpkg dependencies (vcpkg.json) but VCPKG_ROOT isn't set",
            ))?;
            configure.arg(format!(
                "-DCMAKE_TOOLCHAIN_FILE={vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
            ));
        }

        match target {
            // Native host build — cmake/vcpkg pick the host compiler and triplet on their own.
            _ if target == std::env::consts::OS => {},
            "windows" => {
                // Cross-compiles the project itself. Without an explicit VCPKG_TARGET_TRIPLET,
                // vcpkg defaults to the MSVC-based x64-windows triplet for any Windows target —
                // it ignores CMAKE_C_COMPILER/CMAKE_CXX_COMPILER for that guess — so a vcpkg
                // module needs to be told explicitly to resolve its dependencies against the
                // mingw triplet instead (which chainloads scripts/toolchains/mingw.cmake for
                // vcpkg's own port builds; it doesn't affect the outer project's compiler choice
                // below, which is set the same way regardless of vcpkg).
                if has_vcpkg {
                    configure.arg("-DVCPKG_TARGET_TRIPLET=x64-mingw-static");
                }
                configure.args([
                    "-DCMAKE_SYSTEM_NAME=Windows",
                    "-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc",
                    "-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++",
                    // mingw dynamically links libstdc++/libgcc/winpthread by default, so the
                    // .exe needs their DLLs alongside it. Static-link them instead — same
                    // standalone-binary goal as +crt-static in .cargo/config.toml for the
                    // cargo cross-compile path.
                    "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++ -static",
                ]);
            },
            other => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported target: {other}"),
            )),
        }

        if !configure.status()?.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "cmake configure failed"));
        }

        let status = Command::new("cmake").args(["--build", build_dir]).status()?;
        if !status.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "cmake build failed"));
        }

        let (prefix, ext) = self.artifact_naming(target);
        let from = Path::new(build_dir).join(format!("{prefix}{}{ext}", self.name));
        std::fs::copy(from, out)?;

        // Compiled shaders (SPIR-V) are runtime assets the executable loads from disk, so they
        // travel with it. Stopgap until shaders get their own module kind with N artifacts.
        let shaders = Path::new(build_dir).join("shaders");
        if shaders.is_dir() {
            let dest = Path::new(out).parent().unwrap_or_else(|| Path::new(".")).join("shaders");
            fs::create_dir_all(&dest)?;
            for entry in fs::read_dir(&shaders)? {
                let entry = entry?;
                if entry.file_type()?.is_file() {
                    fs::copy(entry.path(), dest.join(entry.file_name()))?;
                }
            }
        }
        Ok(())
    }

    fn save_cmake(&self, project: &Directory) -> Result<(), std::io::Error> {
        let module_dir = Path::new(&self.src).parent().unwrap_or_else(|| Path::new("."));
        let module_dir = module_dir.to_str().expect("module src path is not valid UTF-8");
        let name = &self.name;

        let target = match self.kind.as_str() {
            "library" => format!(
                "add_library({name} SHARED src/{name}.cpp)\ntarget_include_directories({name} PUBLIC include)\n"
            ),
            _ => format!("add_executable({name} src/main.cpp)\n"),
        };

        let cmakelists = format!(
            "cmake_minimum_required(VERSION 3.20)\nproject({name})\n\nset(CMAKE_CXX_STANDARD 17)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n\ninclude(FetchContent)\n\n{DEPS_BEGIN}\n{DEPS_END}\n\n{target}"
        );
        project.write_source(&self.src, &cmakelists)?;

        match self.kind.as_str() {
            "library" => {
                let header = format!(
                    "#pragma once\n\nnamespace {name} {{\n    void hello();\n}}\n"
                );
                let source = format!(
                    "#include <iostream>\n\n#include \"{name}/{name}.hpp\"\n\nnamespace {name} {{\n    void hello() {{\n        std::cout << \"Hello, World!\" << std::endl;\n    }}\n}}\n"
                );
                project.write_source(&format!("{module_dir}/include/{name}/{name}.hpp"), &header)?;
                project.write_source(&format!("{module_dir}/src/{name}.cpp"), &source)
            },
            _ => {
                let main = "#include <iostream>\n\nint main() {\n    std::cout << \"Hello, World!\" << std::endl;\n    return 0;\n}\n";
                project.write_source(&format!("{module_dir}/src/main.cpp"), main)
            },
        }
    }

    fn save_cargo(&self, project: &Directory) -> Result<(), std::io::Error> {
        let manifest_dir = project.path.join(
            Path::new(&self.src).parent().unwrap_or_else(|| Path::new("."))
        );

        match fs::create_dir_all(&manifest_dir) {
            Ok(_) => {
                let status = match manifest_dir.try_exists()? {
                    true => {
                        let mut command = Command::new("cargo");
                        command.args(["init", "--name", &self.name, "--vcs", "none", "--edition", "2024"]);
                        if self.kind == "library" {
                            // Otherwise `cargo init` always scaffolds src/main.rs (a binary
                            // crate), which has no library target for another module to
                            // reference via --module-ref.
                            command.arg("--lib");
                        }
                        command.current_dir(&manifest_dir);
                        command.status()
                    }
                    false => Err(io::Error::new(io::ErrorKind::AlreadyExists, "module already exists")),
                };
                
                match status {
                    Ok(_) => Ok(()),
                    Err(_) => Err(io::Error::new(io::ErrorKind::Other, "cargo init failed")),
                }
            },
            Err(err) => Err(io::Error::new(io::ErrorKind::AlreadyExists, format!("Error creating module: {err}"))),
        }
    }

    pub fn save(&self, project: &Directory) -> Result<(), std::io::Error> {
        let name = self.name.clone();
        let lang = self.lang.clone();
        println!("gram module (name={name}, lang={lang})");
        match self.lang.as_str() {
            "cargo" => self.save_cargo(project),
            "cmake" => self.save_cmake(project),
            other => return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                format!("unsupported module language: {other}"),
            )),
        }
    }
}

impl Directory {
    pub fn new<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let path = path.as_ref().to_path_buf();
        fs::create_dir_all(&path)?;
        Ok(Self { path: path })
    }

    pub fn write_file<Config: serde::Serialize>(&self, filename: &str, config: &Config) -> Result<(), std::io::Error> {
        let pretty_config = ron::ser::PrettyConfig::default().struct_names(true);
        match ron::ser::to_string_pretty(&config, pretty_config) {
            Ok(config) => {
                let mut file = fs::File::create(self.path.join(filename))?;
                file.write_all(config.as_bytes())
            }
            Err(error) => Err(io::Error::new(io::ErrorKind::InvalidData, error)),
        }
    }

    pub fn write_source(&self, relative_path: &str, content: &str) -> Result<(), std::io::Error> {
        let full_path = self.path.join(relative_path);
        if let Some(parent) = full_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let mut file = fs::File::create(full_path)?;
        file.write_all(content.as_bytes())
    }

    pub fn read_source(&self, relative_path: &str) -> io::Result<Option<String>> {
        match fs::read_to_string(self.path.join(relative_path)) {
            Ok(contents) => Ok(Some(contents)),
            Err(err) if err.kind() == io::ErrorKind::NotFound => Ok(None),
            Err(err) => Err(err),
        }
    }
}