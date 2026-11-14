use std::fs;
use std::io;
use std::path::Path;
use std::process::Command;

use crate::config::module::ModuleConfig;
use crate::directory::Directory;
use crate::lang::CMake;
use crate::lang::DependencySpec;
use crate::lang::ModuleBackend;
use crate::lang::ResolvedModule;
use crate::lang::replace_file;
use crate::lang::slash_path;
use crate::lang::vcpkg;
use crate::profile::Profile;

// Marker lines bracketing the block of gram-managed FetchContent dependencies
// inside a cmake module's CMakeLists.txt, so add/remove can edit that region
// as text without parsing the rest of the file.
pub(crate) const DEPS_BEGIN: &str = "# --- gram dependencies (managed, do not edit by hand) ---";
pub(crate) const DEPS_END: &str = "# --- end gram dependencies ---";

impl ModuleBackend for CMake {
    fn manifest_file(&self) -> &'static str {
        "CMakeLists.txt"
    }
    
    fn scaffold(&self, module: &ModuleConfig, project: &Directory) -> io::Result<()> {
        let module_dir = Path::new(&module.src).parent().unwrap_or_else(|| Path::new("."));
        let module_dir = module_dir.to_str().expect("module src path is not valid UTF-8");
        let name = &module.name;

        let target = match module.kind.as_str() {
            "library" => format!(
                "add_library({name} SHARED src/{name}.cpp)\ntarget_include_directories({name} PUBLIC include)\n"
            ),
            _ => format!("add_executable({name} src/main.cpp)\n"),
        };

        let cmakelists = format!(
            "cmake_minimum_required(VERSION 3.20)\nproject({name})\n\nset(CMAKE_CXX_STANDARD 17)\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\n\ninclude(FetchContent)\n\n{DEPS_BEGIN}\n{DEPS_END}\n\n{target}"
        );
        project.write_source(&module.src, &cmakelists)?;

        match module.kind.as_str() {
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
    
    fn compile(&self, module: &ModuleConfig, refs: &[ResolvedModule], out: &str, target: &str, profile: Profile) -> io::Result<()> {
        let manifest_dir = Path::new(&module.src).parent().unwrap_or_else(|| Path::new("."));
        // One build dir per (target, profile): switching configurations keeps the other one's objects.
        let build_dir = manifest_dir.join(format!("build-{target}-{profile}"));
        let config = profile.cmake_config();
        let build_dir = build_dir.to_str().expect("build dir path is not valid UTF-8");
        let manifest_dir = manifest_dir.to_str().expect("module src path is not valid UTF-8");

        let mut configure = Command::new("cmake");
        // --fresh:  and CMakeLists.txt are regenerated on every add/remove,
        // so reconfigure from scratch each build too — otherwise settings that only take
        // effect on a build dir's *first* configure (CMAKE_TOOLCHAIN_FILE among them) go
        // stale once a module gets vcpkg wired up on top of an already-configured build dir.
        // The build type only matters to single-config generators (Makefiles, Ninja), where an empty
        // one means no -g and no -O0: breakpoints would never hit. Multi-config ones pick it in
        // `--build --config` and ignore this.
        configure.args(["-S", manifest_dir, "-B", build_dir, "--fresh", &format!("-DCMAKE_BUILD_TYPE={config}")]);
        // So editors (VS Code's C/C++ extension) can get accurate IntelliSense straight
        // from gram's own build dir, without needing a separate IDE-driven configure.
        configure.arg("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON");
        // Forward slashes: CMake reads backslashes in a -D value as escapes (a Windows host).
        let configurations = write_configurations(Path::new(build_dir))?.display().to_string().replace('\\', "/");
        configure.arg(format!("-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES={configurations}"));

        let vcpkg_manifest = vcpkg::build_manifest(module, refs, Path::new(build_dir))?;
        let has_vcpkg = vcpkg_manifest.is_some();
        if let Some(manifest) = &vcpkg_manifest {
            let vcpkg_root = std::env::var("VCPKG_ROOT").map_err(|_| io::Error::new(
                io::ErrorKind::NotFound,
                "module declares vcpkg dependencies (vcpkg.json) but VCPKG_ROOT isn't set",
            ))?;
            configure.arg(format!(
                "-DCMAKE_TOOLCHAIN_FILE={vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
            ));
            if let vcpkg::BuildManifest::Merged(dir) = manifest {
                configure.arg(format!("-DVCPKG_MANIFEST_DIR={}", dir.display()));
            }
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

        // --config only matters to multi-config generators (Visual Studio): it picks the configuration
        // there (Debug without it), and the artifact is looked for in the folder of the same name below.
        let status = Command::new("cmake").args(["--build", build_dir, "--config", config]).status()?;
        if !status.success() {
            return Err(io::Error::new(io::ErrorKind::Other, "cmake build failed"));
        }

        // The CMakeLists decides whether a library is SHARED or STATIC, not gram, so look for
        // every naming the module could have produced. The copy keeps the real file name.
        // A multi-config generator (Visual Studio) puts the output one folder down, in <build dir>/<Config>/.
        let candidates: Vec<_> = [Path::new(build_dir).to_path_buf(), Path::new(build_dir).join(config)].iter()
            .flat_map(|dir| artifact_candidates(module, target).into_iter().map(move |file| dir.join(file)))
            .collect();
        let from = candidates.iter().find(|path| path.exists()).ok_or_else(|| io::Error::new(
            io::ErrorKind::NotFound,
            format!(
                "cmake built module {} but its artifact wasn't found (looked for {})",
                module.name,
                candidates.iter().map(|path| path.display().to_string()).collect::<Vec<_>>().join(", ")
            ),
        ))?;
        // An executable takes the name gram picked (`out`: the project's name for a main module);
        // a library keeps its real one, since gram can't know whether it's static or shared.
        let dest = match module.kind.as_str() {
            "library" => Path::new(out).with_file_name(from.file_name().expect("artifact candidates are file names")),
            _ => Path::new(out).to_path_buf(),
        };
        replace_file(&from, &dest)?;

        // With a multi-config generator (Visual Studio) vcpkg's toolchain copies the DLLs an
        // executable needs (glfw3.dll, vulkan-1.dll, ...) next to it, in <build dir>/<Config>/.
        // Without them Windows won't start the program at all (exit code 0xC0000135, no window,
        // no output), so they travel with it. Single-config builds don't get this treatment.
        let from_dir = from.parent().expect("artifact candidates are file names");
        if module.kind == "runtime" && from_dir != Path::new(build_dir) {
            let dll_dir = Path::new(out).parent().unwrap_or_else(|| Path::new("."));
            for entry in fs::read_dir(from_dir)? {
                let path = entry?.path();
                if path.extension().is_some_and(|extension| extension.eq_ignore_ascii_case("dll")) {
                    replace_file(&path, &dll_dir.join(path.file_name().expect("a directory entry has a name")))?;
                }
            }
        }

        // Runtime assets the executable loads from disk travel with it: compiled shaders (SPIR-V)
        // and textures, each in its own folder of the build dir (see GramShaders.cmake /
        // GramTextures.cmake). Stopgap until assets get their own module kind with N artifacts.
        const ASSET_DIRS: [&str; 2] = ["shaders", "textures"];
        let out_dir = Path::new(out).parent().unwrap_or_else(|| Path::new("."));
        for asset_dir in ASSET_DIRS {
            let source = Path::new(build_dir).join(asset_dir);
            if !source.is_dir() {
                continue;
            }
            let dest = out_dir.join(asset_dir);
            fs::create_dir_all(&dest)?;
            for entry in fs::read_dir(&source)? {
                let entry = entry?;
                if entry.file_type()?.is_file() {
                    fs::copy(entry.path(), dest.join(entry.file_name()))?;
                }
            }
        }
        Ok(())
    }
    
    /// A "library" module produces a shared library (lib*.so / lib*.dylib / *.dll) instead
    /// of the default executable naming.
    fn artifact_naming(&self, module: &ModuleConfig, target: &str) -> (&'static str, &'static str) {
        match (module.kind.as_str(), target) {
            ("library", "windows") => ("", ".dll"),
            ("library", "macos")   => ("lib", ".dylib"),
            ("library", _)         => ("lib", ".so"),
            (_, "windows")         => ("", ".exe"),
            _                      => ("", ""),
        }
    }
    
    fn add_dependency(&self, module: &mut ModuleConfig, dep: &DependencySpec) -> io::Result<()> {
        // Registry: a plain name with neither --url nor --vcpkg, which cmake has nowhere to resolve from.
        const NEEDS_SOURCE: &str = "cmake dependencies need --url (used as FetchContent's GIT_REPOSITORY), or pass --vcpkg to resolve from a vcpkg registry instead";

        match dep {
            DependencySpec::Vcpkg { .. } => vcpkg::add_dependency(module, dep),
            DependencySpec::Registry { .. } => Err(io::Error::new(io::ErrorKind::InvalidInput, NEEDS_SOURCE)),
            DependencySpec::Module { .. } => Err(io::Error::new(io::ErrorKind::InvalidInput, "module references go through add_module_ref (they need resolving against Gram.ron first)")),
            DependencySpec::Git { name, url, tag } => {
                if url.is_empty() {
                    return Err(io::Error::new(io::ErrorKind::InvalidInput, NEEDS_SOURCE));
                }

                println!("gram add (dependency={name}, src={}): adding the new dependency", module.src);
                let contents = fs::read_to_string(&module.src)?;
                let begin_marker = format!("# gram:dep:{name} begin");

                if !contents.contains(&begin_marker) {
                    let tag_line = if tag.is_empty() { String::new() } else { format!("\n  GIT_TAG {tag}") };
                    let block = format!(
                        "{begin_marker}\nFetchContent_Declare(\n  {name}\n  GIT_REPOSITORY {url}{tag_line}\n)\nFetchContent_MakeAvailable({name})\n# gram:dep:{name} end\n"
                    );
                    let contents = contents.replacen(DEPS_END, &format!("{block}{DEPS_END}"), 1);
                    fs::write(&module.src, contents)?;
                }

                module.deps.push(dep.clone());
                Ok(())
            }
        }
    }
    
    fn rm_dependency(&self, module: &mut ModuleConfig, dependency: &str) -> io::Result<String> {
        println!("gram remove (dependency={dependency}, src={}): removing the dependency", module.src);
        let contents = fs::read_to_string(&module.src)?;
        let begin_marker = format!("# gram:dep:{dependency} begin");
        let end_marker = format!("# gram:dep:{dependency} end");

        let (start, end) = match (contents.find(&begin_marker), contents.find(&end_marker)) {
            (Some(start), Some(end)) => (start, end + end_marker.len()),
            _ => return Err(io::Error::new(io::ErrorKind::Other, "dependency not found in CMakeLists.txt")),
        };

        let mut new_contents = contents[..start].to_string();
        new_contents.push_str(contents[end..].trim_start_matches('\n'));

        // A module-ref dependency also gets a target_link_libraries block appended after
        // the target (see add_module_ref) — strip it too if present. Every other
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

        fs::write(&module.src, new_contents)?;

        vcpkg::remove_dependency(module, dependency)?;

        match module.deps.iter().position(|dep| dep.name() == dependency) {
            Some(position) => Ok(module.deps.swap_remove(position).name()),
            None => Err(io::Error::new(io::ErrorKind::Other, "dependency not found in module config")),
        }
    }
    
    fn add_module_ref(&self, module: &mut ModuleConfig, resolved: &ResolvedModule) -> io::Result<()> {
        let name = &resolved.name;
        println!("gram add (dependency={name}, src={}): adding module reference (add_subdirectory)", module.src);
        let contents = fs::read_to_string(&module.src)?;
        let begin_marker = format!("# gram:dep:{name} begin");

        let contents = if !contents.contains(&begin_marker) {
            let rel = slash_path(&resolved.rel_dir);
            let block = format!(
                "{begin_marker}\nadd_subdirectory(${{CMAKE_CURRENT_SOURCE_DIR}}/{rel} ${{CMAKE_CURRENT_BINARY_DIR}}/{name})\n# gram:dep:{name} end\n"
            );
            contents.replacen(DEPS_END, &format!("{block}{DEPS_END}"), 1)
        } else {
            contents
        };

        // target_link_libraries needs the target (add_executable/add_library) to already
        // exist, so it can't live in the DEPS region above — appended after it instead.
        let link_marker = format!("# gram:link:{name} begin");
        let contents = if !contents.contains(&link_marker) {
            format!(
                "{contents}\n{link_marker}\ntarget_link_libraries({} PRIVATE {name})\n# gram:link:{name} end\n",
                module.name
            )
        } else {
            contents
        };

        fs::write(&module.src, contents)?;
        module.deps.push(DependencySpec::Module { name: resolved.name.clone() });
        Ok(())
    }
}

/// The build configurations gram knows (Debug/Release/Final, see `Profile`) taught to CMake. The first
/// file runs at the top-level `project()`, before the compiler is known (configuration list, LTO and the
/// imported-config mapping for Final); it registers the second as `CMAKE_USER_MAKE_RULES_OVERRIDE`, which
/// runs once the compiler is known and derives each configuration's flags from the compiler's own.
const CONFIGURATIONS: &str = include_str!("GramConfigurations.cmake");
const CONFIGURATION_FLAGS: &str = include_str!("GramConfigurationFlags.cmake");

/// Writes the two configuration files into `<build dir>/gram/` and returns the first one's absolute path,
/// for `CMAKE_PROJECT_TOP_LEVEL_INCLUDES` (it finds the second through `CMAKE_CURRENT_LIST_DIR`).
fn write_configurations(build_dir: &Path) -> io::Result<std::path::PathBuf> {
    let dir = std::env::current_dir()?.join(build_dir).join("gram");
    fs::create_dir_all(&dir)?;
    fs::write(dir.join("GramConfigurationFlags.cmake"), CONFIGURATION_FLAGS)?;
    let configurations = dir.join("GramConfigurations.cmake");
    fs::write(&configurations, CONFIGURATIONS)?;
    Ok(configurations)
}

/// File names a cmake module's artifact can have on `target`. A runtime module is an
/// executable; a library is either shared (the naming `artifact_naming` reports, used by
/// gram's own scaffold) or static (lib*.a, which mingw also produces for Windows; <name>.lib
/// with MSVC).
fn artifact_candidates(module: &ModuleConfig, target: &str) -> Vec<String> {
    let (prefix, ext) = CMake.artifact_naming(module, target);
    let name = &module.name;
    let mut candidates = vec![format!("{prefix}{name}{ext}")];
    if module.kind == "library" {
        candidates.push(format!("lib{name}.a"));
        if target == "windows" {
            candidates.push(format!("{name}.lib"));
        }
    }
    candidates
}
