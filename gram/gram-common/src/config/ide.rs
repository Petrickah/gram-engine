//! `gram ide xcode|vs`: one IDE project for the whole tree. gram writes a CMake superproject
//! (`build/ide/<ide>/CMakeLists.txt`) holding the tree's cmake modules and a custom target per cargo
//! `dynamic` module that runs `gram build --module <name>`, then lets CMake's Xcode / Visual Studio
//! generator turn it into the IDE's project.

use std::fmt::Write;
use std::fmt::Display;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

use clap::ValueEnum;

use crate::config::BuildConfig;
use crate::config::module::ModuleConfig;
use crate::lang::DependencySpec;
use crate::lang::cmake::write_configurations;
use crate::lang::slash_path;
use crate::lang::vcpkg;

#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Ide { Xcode, Vs }

impl Display for Ide {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Ide::Xcode => write!(f, "xcode"),
            Ide::Vs    => write!(f, "vs"),
        }
    }
}

/// Defined once in the superproject, called per cmake module: a module may hold several targets
/// (GramEngine: GramRenderer, GramScripting, GramEngine), and the IDE should show each header under
/// the one its sources belong to. A header goes to the only target of the module's directory compiling
/// a source under the header's folder, `include/` read as `src/` (include/Gram/Renderer/ →
/// src/Gram/Renderer/ → GramRenderer); when none or several do (include/Gram/), to the module's target.
const HEADERS_FUNCTION: &str = r#"
function(gram_ide_headers dir fallback)
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${dir}/*.hpp" "${dir}/*.h")
    list(FILTER headers EXCLUDE REGEX "/(target|build[^/]*)/")
    source_group(TREE "${dir}" FILES ${headers})
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(header IN LISTS headers)
        get_filename_component(folder "${header}" DIRECTORY)
        file(RELATIVE_PATH folder "${dir}" "${folder}")
        string(REGEX REPLACE "^include(/|$)" "src\\1" folder "${folder}")
        set(owners "")
        foreach(target IN LISTS targets)
            get_target_property(sources ${target} SOURCES)
            get_target_property(source_dir ${target} SOURCE_DIR)
            foreach(source IN LISTS sources)
                get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${source_dir}")
                file(RELATIVE_PATH source "${dir}" "${source}")
                string(FIND "${source}" "${folder}/" at)
                if(at EQUAL 0)
                    list(APPEND owners ${target})
                    break()
                endif()
            endforeach()
        endforeach()
        list(LENGTH owners count)
        if(count EQUAL 1)
            target_sources(${owners} PRIVATE "${header}")
        else()
            target_sources(${fallback} PRIVATE "${header}")
        endif()
    endforeach()
endfunction()
"#;

impl BuildConfig {
    /// The cmake modules no other cmake module references: the superproject adds these, the others
    /// come in through them (`Runtime` brings `GramEngine`, which would otherwise be added twice).
    pub(crate) fn ide_roots(&self) -> Vec<&ModuleConfig> {
        let cmake = || self.modules.iter().filter(|m| m.lang == "cmake");
        cmake()
            .filter(|module| !cmake().any(|other| other.deps.iter().any(|dep| matches!(dep, DependencySpec::Module { name } if *name == module.name))))
            .collect()
    }

    pub(crate) fn cargo_dynamic(&self) -> Vec<&ModuleConfig> {
        self.modules.iter().filter(|m| m.lang == "cargo" && m.kind == "dynamic").collect()
    }

    /// The files the IDE shows next to the ones it compiles, appended to the superproject: each cmake
    /// module's headers (on the target compiling the sources next to them: `gram_ide_headers`), and each cargo dynamic
    /// module's and the cargo modules it references' Rust code, schemas, shaders and manifests (on a
    /// custom target per module: the dynamic one's own, which already builds it, or one holding only
    /// files). Globbed by CMake with CONFIGURE_DEPENDS: a new file shows up at the next build, without
    /// running `gram ide` again. `target/` and `build*/` are left out; folders are kept (source_group).
    pub(crate) fn ide_sources(&self, root: &Path) -> String {
        let root = root.display().to_string().replace('\\', "/");
        let dynamic = self.cargo_dynamic();
        let referenced: Vec<&ModuleConfig> = self.modules.iter()
            .filter(|m| m.lang == "cargo" && m.kind != "dynamic")
            .filter(|m| dynamic.iter().any(|d| d.deps.iter().any(|dep| matches!(dep, DependencySpec::Module { name } if *name == m.name))))
            .collect();

        let mut out = String::new();
        let files = |out: &mut String, module: &ModuleConfig, patterns: &[&str]| {
            let dir = format!("{root}/{}", slash_path(module.dir()));
            let var = format!("{}_ide_files", module.name.replace(|c: char| !c.is_ascii_alphanumeric(), "_"));
            let globs: Vec<String> = patterns.iter().map(|pattern| format!("\"{dir}/{pattern}\"")).collect();
            writeln!(out).unwrap();
            writeln!(out, "file(GLOB_RECURSE {var} CONFIGURE_DEPENDS {})", globs.join(" ")).unwrap();
            writeln!(out, "list(FILTER {var} EXCLUDE REGEX \"/(target|build[^/]*)/\")").unwrap();
            writeln!(out, "source_group(TREE \"{dir}\" FILES ${{{var}}})").unwrap();
            // Shown, never compiled: Visual Studio would otherwise run its own HLSL compiler (FXC) on the
            // .hlsl files, which dxc compiles (GramShaders.cmake). HEADER_FILE_ONLY isn't enough there:
            // VS_TOOL_OVERRIDE "None" turns its FXCompile items into plain files.
            writeln!(out, "set_source_files_properties(${{{var}}} PROPERTIES HEADER_FILE_ONLY ON VS_TOOL_OVERRIDE \"None\")").unwrap();
            var
        };

        let cmake: Vec<&ModuleConfig> = self.modules.iter().filter(|m| m.lang == "cmake").collect();
        if !cmake.is_empty() {
            out.push_str(HEADERS_FUNCTION);
        }
        for module in cmake {
            writeln!(out, "gram_ide_headers(\"{root}/{}\" {})", slash_path(module.dir()), module.name).unwrap();
        }
        let rust = ["*.rs", "*.ron", "*.toml", "*.hlsl"];
        for module in &dynamic {
            let var = files(&mut out, module, &rust);
            writeln!(out, "target_sources({} PRIVATE ${{{var}}})", module.name).unwrap();
        }
        for module in referenced {
            let var = files(&mut out, module, &rust);
            writeln!(out, "add_custom_target({} SOURCES ${{{var}}})", module.name).unwrap();
        }
        out
    }

    /// The superproject's CMakeLists.txt. `root` is the project's root (absolute), `gram` this gram
    /// executable, `host` the OS the IDE runs on (the output folder's target), `path` the PATH gram runs
    /// with from the IDE: an IDE opened from the Finder or `open` doesn't get the shell's (no cargo).
    pub(crate) fn superproject(&self, root: &Path, gram: &Path, host: &str, path: &str) -> io::Result<String> {
        // Forward slashes: CMake reads a backslash as an escape (a Windows host).
        let forward = |path: &Path| path.display().to_string().replace('\\', "/");
        let (root, gram) = (forward(root), forward(gram));
        // The PATH stays as it is (backslashes are Windows' own), escaped for CMake: `\\` for a backslash,
        // and `$<SEMICOLON>` for Windows' separator, which CMake would otherwise split the argument on.
        let path = path.replace('\\', "\\\\").replace(';', "$<SEMICOLON>");
        let mut out = String::new();
        writeln!(out, "# Generated by `gram ide` from Gram.ron: don't edit it, run `gram ide` again instead.").unwrap();
        writeln!(out, "cmake_minimum_required(VERSION 3.24)").unwrap();
        writeln!(out, "project({} LANGUAGES C CXX)", &self.name).unwrap();

        for module in self.ide_roots() {
            writeln!(out).unwrap();
            writeln!(out, "add_subdirectory(\"{root}/{}\" \"${{CMAKE_BINARY_DIR}}/{}\")", slash_path(module.dir()), module.name).unwrap();
        }

        for module in self.cargo_dynamic() {
            writeln!(out).unwrap();
            writeln!(out, "add_custom_target({} ALL", module.name).unwrap();
            writeln!(out, "    COMMAND ${{CMAKE_COMMAND}} -E env \"PATH={path}\" \"{gram}\" build --module {} --config $<LOWER_CASE:$<CONFIG>>", module.name).unwrap();
            writeln!(out, "    WORKING_DIRECTORY \"{root}\"").unwrap();
            writeln!(out, "    USES_TERMINAL)").unwrap();
        }

        // Visual Studio's F5 starts the startup project, ALL_BUILD unless told otherwise: the first runtime
        // root (the game). Other generators ignore the property.
        if let Some(runtime) = self.ide_roots().into_iter().find(|module| module.kind == "runtime") {
            writeln!(out).unwrap();
            writeln!(out, "set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT {})", runtime.name).unwrap();
        }

        for module in self.ide_roots() {
            if module.kind == "runtime" {
                writeln!(out).unwrap();
                writeln!(out, "set_target_properties({} PROPERTIES", module.name).unwrap();
                writeln!(out, "    OUTPUT_NAME \"{}\"", self.artifact_name(module)).unwrap();
                writeln!(out, "    RUNTIME_OUTPUT_DIRECTORY \"{}/build/{}/{}/$<LOWER_CASE:$<CONFIG>>\")", root, self.name, host).unwrap();
                // Built first: the game loads them, and gram build generates the components' header too.
                for dynamic in self.cargo_dynamic() {
                    writeln!(out, "add_dependencies({} {})", module.name, dynamic.name).unwrap();
                }
            }
        }

        Ok(out)
    }

    /// Writes the superproject into `build/ide/<ide>/` and, unless `generate_only`, configures it with
    /// the IDE's generator. Returns the project file to open (or the CMakeLists.txt, if generate_only).
    pub fn ide(&self, ide: Ide, generate_only: bool, open: bool) -> io::Result<PathBuf> {
        let host = std::env::consts::OS;
        let generator = match (ide, host) {
            (Ide::Xcode, "macos")  => "Xcode",
            (Ide::Vs, "windows")   => "Visual Studio 18 2026",
            (Ide::Xcode, _)        => return Err(io::Error::new(io::ErrorKind::Unsupported, "Xcode projects can only be generated on macOS")),
            (Ide::Vs, _)           => return Err(io::Error::new(io::ErrorKind::Unsupported, "Visual Studio projects can only be generated on Windows")),
        };

        let root = std::env::current_dir()?;
        let dir = PathBuf::from(format!("build/ide/{ide}"));
        fs::create_dir_all(&dir)?;
        let lists = dir.join("CMakeLists.txt");
        let superproject = self.superproject(&root, &std::env::current_exe()?, host, &std::env::var("PATH").unwrap_or_default())?;
        fs::write(&lists, superproject + &self.ide_sources(&root))?;
        println!("gram ide (name={}, ide={ide}): wrote {}", self.name, lists.display());
        if generate_only {
            return Ok(lists);
        }

        // The configure is the same as a module's (lang/cmake): the configurations, vcpkg with the
        // manifest of the modules the superproject adds, merged with the ones they reference.
        let dir_str = dir.to_str().expect("build dir path is not valid UTF-8");
        let mut configure = crate::toolchain::cmake()?;
        configure.args(["-S", dir_str, "-B", dir_str, "--fresh", "-G", generator]);
        let configurations = write_configurations(&dir)?.display().to_string().replace('\\', "/");
        configure.arg(format!("-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES={configurations}"));

        let mut manifest = None;
        for module in self.ide_roots() {
            let found = vcpkg::build_manifest(module, &self.module_refs(module)?, &dir)?;
            if let Some(found) = found {
                if manifest.is_some() {
                    return Err(io::Error::new(io::ErrorKind::Unsupported, "more than one top-level cmake module uses vcpkg: not supported by gram ide yet"));
                }
                manifest = Some(match found {
                    vcpkg::BuildManifest::Own => root.join(module.dir()),
                    vcpkg::BuildManifest::Merged(merged) => root.join(merged),
                });
            }
        }
        if let Some(manifest) = manifest {
            let vcpkg_root = std::env::var("VCPKG_ROOT").map_err(|_| io::Error::new(
                io::ErrorKind::NotFound,
                "the project declares vcpkg dependencies (vcpkg.json) but VCPKG_ROOT isn't set",
            ))?;
            configure.arg(format!("-DCMAKE_TOOLCHAIN_FILE={vcpkg_root}/scripts/buildsystems/vcpkg.cmake"));
            configure.arg(format!("-DVCPKG_MANIFEST_DIR={}", manifest.display().to_string().replace('\\', "/")));
        }

        if ide == Ide::Xcode {
            // Xcode builds every "standard" architecture in Release by default, which vcpkg (the host's
            // only) can't link: pin the host's. One scheme per target, the game's among them.
            let arch = if std::env::consts::ARCH == "aarch64" { "arm64" } else { std::env::consts::ARCH };
            configure.arg(format!("-DCMAKE_OSX_ARCHITECTURES={arch}"));
            configure.arg("-DCMAKE_XCODE_GENERATE_SCHEME=ON");
            // CMake asks xcodebuild about Xcode, which fails while xcode-select points at the Command
            // Line Tools (which clangd and plain builds are happy with): point this one run at Xcode.
            let selected = Command::new("xcodebuild").arg("-version").output().is_ok_and(|out| out.status.success());
            let xcode = Path::new("/Applications/Xcode.app/Contents/Developer");
            if !selected && xcode.exists() {
                configure.env("DEVELOPER_DIR", xcode);
            }
        }

        if !configure.status()?.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "cmake configure failed"));
        }

        let project = match ide {
            Ide::Xcode => dir.join(format!("{}.xcodeproj", self.name)),
            // CMake writes the XML solution (.slnx) for Visual Studio 2026, the classic .sln before it.
            Ide::Vs    => ["slnx", "sln"].iter()
                              .map(|extension| dir.join(format!("{}.{extension}", self.name)))
                              .find(|path| path.exists())
                              .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, format!("cmake wrote no {}.slnx or .sln in {}", self.name, dir.display())))?,
        };
        if open {
            let status = match ide {
                Ide::Xcode => Command::new("open").arg(&project).status()?,
                Ide::Vs    => Command::new("cmd").args(["/C", "start", ""]).arg(&project).status()?,
            };
            if !status.success() {
                return Err(io::Error::new(io::ErrorKind::Other, format!("couldn't open {}", project.display())));
            }
        }
        Ok(project)
    }
}

#[cfg(test)]
mod tests {
    use std::path::Path;

    use crate::config::BuildConfig;
    use crate::config::module::ModuleConfig;
    use crate::lang::DependencySpec;

    fn module(name: &str, lang: &str, kind: &str, src: &str, deps: &[&str]) -> ModuleConfig {
        ModuleConfig {
            name: name.into(), lang: lang.into(), kind: kind.into(), src: src.into(),
            deps: deps.iter().map(|dep| DependencySpec::Module { name: (*dep).into() }).collect(),
            targets: vec!["macos".into()],
            project: String::new(),
        }
    }

    /// The shape of the real tree: a game runtime referencing an engine library, a Rust library
    /// the game loads, a Rust library only that one uses.
    fn demo() -> BuildConfig {
        let mut config = BuildConfig::new("demo", "0.1.0").unwrap();
        config.set_main("App");
        config.modules = vec![
            module("App", "cmake", "runtime", "App/CMakeLists.txt", &["Engine"]),
            module("Engine", "cmake", "library", "Engine/CMakeLists.txt", &[]),
            module("Logic", "cargo", "dynamic", "Logic/Cargo.toml", &["core"]),
            module("core", "cargo", "library", "core/Cargo.toml", &[]),
        ];
        config
    }

    #[test]
    fn only_unreferenced_cmake_modules_are_roots() {
        let config = demo();
        let roots: Vec<&str> = config.ide_roots().iter().map(|m| m.name.as_str()).collect();
        assert_eq!(roots, ["App"]);
    }

    #[test]
    fn the_ide_shows_headers_and_the_rust_side() {
        let text = demo().ide_sources(Path::new("/root"));
        assert_eq!(text.matches("function(gram_ide_headers dir fallback)").count(), 1);
        assert!(text.contains(r#"gram_ide_headers("/root/App" App)"#));
        assert!(text.contains(r#"gram_ide_headers("/root/Engine" Engine)"#));
        assert!(text.contains(r#""/root/Logic/*.rs" "/root/Logic/*.ron" "/root/Logic/*.toml" "/root/Logic/*.hlsl""#));
        assert!(text.contains("target_sources(Logic PRIVATE ${Logic_ide_files})"));
        assert!(text.contains("add_custom_target(core SOURCES ${core_ide_files})"));
        assert!(text.contains(r#"list(FILTER Logic_ide_files EXCLUDE REGEX "/(target|build[^/]*)/")"#));
        assert!(text.contains("set_source_files_properties(${Logic_ide_files} PROPERTIES HEADER_FILE_ONLY ON VS_TOOL_OVERRIDE \"None\")"));
    }

    #[test]
    fn superproject_holds_the_whole_tree() {
        let text = demo().superproject(Path::new("/root"), Path::new("/bin/gram"), "macos", "/home/.cargo/bin:/usr/bin").unwrap();
        assert_eq!(text, r#"# Generated by `gram ide` from Gram.ron: don't edit it, run `gram ide` again instead.
cmake_minimum_required(VERSION 3.24)
project(demo LANGUAGES C CXX)

add_subdirectory("/root/App" "${CMAKE_BINARY_DIR}/App")

add_custom_target(Logic ALL
    COMMAND ${CMAKE_COMMAND} -E env "PATH=/home/.cargo/bin:/usr/bin" "/bin/gram" build --module Logic --config $<LOWER_CASE:$<CONFIG>>
    WORKING_DIRECTORY "/root"
    USES_TERMINAL)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT App)

set_target_properties(App PROPERTIES
    OUTPUT_NAME "demo"
    RUNTIME_OUTPUT_DIRECTORY "/root/build/demo/macos/$<LOWER_CASE:$<CONFIG>>")
add_dependencies(App Logic)
"#);
    }

    #[test]
    fn a_windows_path_is_escaped_for_cmake() {
        let text = demo().superproject(Path::new(r"C:\root"), Path::new(r"C:\bin\gram.exe"), "windows", r"C:\Users\me\.cargo\bin;C:\WINDOWS").unwrap();
        assert!(text.contains(r#"-E env "PATH=C:\\Users\\me\\.cargo\\bin$<SEMICOLON>C:\\WINDOWS" "C:/bin/gram.exe" build --module Logic"#), "{text}");
        assert!(text.contains(r#"WORKING_DIRECTORY "C:/root""#));
        assert!(text.contains(r#"RUNTIME_OUTPUT_DIRECTORY "C:/root/build/demo/windows/$<LOWER_CASE:$<CONFIG>>""#));
    }
}
