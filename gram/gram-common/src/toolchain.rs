use std::{fmt::Display, io, str::FromStr};
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Toolchain { Native, Mingw, Msvc }   // Native: the system compiler CMake picks on Linux/macOS (GCC, Apple clang)

// "native" | "mingw" | "msvc"
impl FromStr for Toolchain {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "native" => Ok(Toolchain::Native),
            "mingw"  => Ok(Toolchain::Mingw),
            "msvc"   => Ok(Toolchain::Msvc),
            unknown => Err(format!("unknown toolchain: {unknown} (only native, mingw or msvc are available)!"))
        }
    }
}

impl Display for Toolchain {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Toolchain::Native => write!(f, "native"),
            Toolchain::Mingw  => write!(f, "mingw"),
            Toolchain::Msvc   => write!(f, "msvc"),
        }
    }
}

/// What resolve chose, and whether it fell back: the CLI prints the warning, the core only reports.
#[derive(Debug, PartialEq)]
pub struct Resolution { pub toolchain: Toolchain, pub fell_back_from: Option<Toolchain> }

/// The toolchains that can build `target` from `host`, the preferred one first.
fn candidates(host: &str, target: &str) -> &'static [Toolchain] {
    match (host, target) {
        ("linux", "linux")   | ("macos", "macos")   => &[Toolchain::Native],
        ("linux", "windows") | ("macos", "windows") => &[Toolchain::Mingw],
        ("windows", "windows")                      => &[Toolchain::Msvc, Toolchain::Mingw],
        _ => &[],
    }
}

/// `host` and `target` are OS names ("linux"/"macos"/"windows"); `installed` says whether a toolchain
/// is on this machine (in production: a PATH lookup, later vswhere; in tests: a closure).
///
/// Errors: `Unsupported` when nothing can build `target` from `host`, `InvalidInput` when the requested
/// toolchain can't, `NotFound` when the toolchain (or every candidate) isn't installed.
pub fn resolve(host: &str, target: &str, requested: Option<Toolchain>, installed: &dyn Fn(Toolchain) -> bool) -> io::Result<Resolution> {
    let candidates = candidates(host, target);
    if candidates.is_empty() {
        return Err(io::Error::new(io::ErrorKind::Unsupported, format!("can't build target {target} from a {host} host")))
    }

    match requested {
        // Explicit: no fallback. It must be one of the candidates, and installed.
        Some(toolchain) => if !candidates.contains(&toolchain) { Err(io::Error::new(io::ErrorKind::InvalidInput, format!("{toolchain} can't build target {target} from a {host} host"))) }
                                      else if !installed(toolchain) { Err(io::Error::new(io::ErrorKind::NotFound, format!("{toolchain} isn't installed (needed to build target {target})"))) }
                                      else { Ok(Resolution { toolchain, fell_back_from: None }) },
        // Implicit: the first installed candidate; a fallback when it isn't the preferred one.
        None => candidates.iter().copied()
                    .find(|t| installed(*t))
                    .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, format!("no toolchain to build target {target} from a {host} host is installed (looked for: {})", candidates.iter().map(|t| t.to_string()).collect::<Vec<_>>().join(", "))))
                    .map(|t| Resolution { toolchain: t, fell_back_from: (t != candidates[0]).then_some(candidates[0]) })
    }
}

/// Everything a toolchain changes in a build. Empty / None = let cmake and cargo pick on their own.
#[derive(Debug, Default, PartialEq)]
pub struct ToolchainSettings {
    pub generator: Option<&'static str>,      // cmake -G
    pub cmake_args: Vec<&'static str>,        // extra -D... for configure
    pub vcpkg_triplet: Option<&'static str>,  // -DVCPKG_TARGET_TRIPLET, only when the module uses vcpkg
    pub cargo_triple: Option<&'static str>,   // cargo --target
}

/// What `toolchain` sets to build `target` from `host`. Native and msvc set nothing: cmake and cargo pick
/// the host's compiler on their own. Called after `resolve`, so the combination is valid; anything else
/// gets the defaults too.
pub fn settings(host: &str, target: &str, toolchain: Toolchain) -> ToolchainSettings {
    match (host, target, toolchain) {
        ("windows", "windows", Toolchain::Mingw) => ToolchainSettings {
            generator: Some("MinGW Makefiles"),
            cmake_args: [
                "-DCMAKE_C_COMPILER=gcc",
                "-DCMAKE_CXX_COMPILER=g++",
                "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++ -static"
            ].to_vec(),
            vcpkg_triplet: Some("x64-mingw-static"),
            cargo_triple: Some("x86_64-pc-windows-gnu")
        },
        ("linux", "windows", Toolchain::Mingw) | ("macos", "windows", Toolchain::Mingw) => ToolchainSettings {
            generator: None,
            cmake_args: [
                "-DCMAKE_SYSTEM_NAME=Windows",
                "-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc",
                "-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++",
                "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++ -static"
            ].to_vec(),
            vcpkg_triplet: Some("x64-mingw-static"),
            cargo_triple: Some("x86_64-pc-windows-gnu")
        },
        _ => ToolchainSettings::default(),
    }
}


/// Whether `toolchain` is on this machine, for `resolve`. Native is always there: gram never checked
/// the system compiler, CMake finds (or fails to find) it. Mingw is found by its compiler on the PATH:
/// `x86_64-w64-mingw32-gcc` for a cross build, `gcc` on Windows. Msvc by `cl` on the PATH (a Developer
/// Console) or, from a plain terminal, by vswhere: the Visual Studio generator finds the compiler on
/// its own, without the Developer Console's environment.
pub fn installed(toolchain: Toolchain) -> bool {
    match (toolchain, std::env::consts::OS) {
        (Toolchain::Native, _)        => true,
        (Toolchain::Mingw, "windows") => on_path("gcc"),
        (Toolchain::Mingw, _)         => on_path("x86_64-w64-mingw32-gcc"),
        (Toolchain::Msvc, "windows")  => on_path("cl") || visual_studio().is_some(),
        (Toolchain::Msvc, _)          => false,
    }
}

/// The installation folder of the newest Visual Studio with the C++ tools, asked once from vswhere
/// (installed with Visual Studio, always at the same place). None off Windows or without one.
pub fn visual_studio() -> Option<&'static Path> {
    static FOUND: OnceLock<Option<PathBuf>> = OnceLock::new();
    FOUND.get_or_init(|| {
        let program_files = std::env::var_os("ProgramFiles(x86)")?;
        let vswhere = Path::new(&program_files).join(r"Microsoft Visual Studio\Installer\vswhere.exe");
        let out = Command::new(vswhere)
            .args(["-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"])
            .output().ok()?;
        let path = String::from_utf8(out.stdout).ok()?;
        let path = path.lines().next()?.trim();
        (out.status.success() && !path.is_empty()).then(|| PathBuf::from(path))
    }).as_deref()
}

/// The cmake to run: the one on the PATH, else (Windows, a plain terminal) the one Visual Studio ships
/// with. Errors when there's neither, instead of the OS's "program not found".
pub fn cmake() -> io::Result<Command> {
    if on_path("cmake") {
        return Ok(Command::new("cmake"));
    }
    let bundled = visual_studio()
        .map(|vs| vs.join(r"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"))
        .filter(|cmake| cmake.is_file());
    match bundled {
        Some(cmake) => Ok(Command::new(cmake)),
        None => Err(io::Error::new(io::ErrorKind::NotFound, "cmake isn't on the PATH (and no Visual Studio with CMake was found): install CMake, or run gram from a Developer PowerShell")),
    }
}

/// Whether an executable named `program` is in one of the PATH's directories.
fn on_path(program: &str) -> bool {
    let file = format!("{program}{}", std::env::consts::EXE_SUFFIX);
    std::env::var_os("PATH")
        .is_some_and(|path| std::env::split_paths(&path).any(|dir| dir.join(&file).is_file()))
}

#[cfg(test)]
mod tests {
    use std::io;
    use crate::toolchain::{Toolchain, ToolchainSettings, resolve, settings};

    fn only(available: &'static [Toolchain]) -> impl Fn(Toolchain) -> bool {
        move |t| available.contains(&t)
    }

    #[test]
    fn toolchain_names_round_trip() {
        for t in [Toolchain::Native, Toolchain::Mingw, Toolchain::Msvc] {
            assert_eq!(t.to_string().parse::<Toolchain>(), Ok(t));
        }
        assert!("clang".parse::<Toolchain>().is_err());
    }

    #[test]
    fn native_on_the_same_host() {
        for os in ["macos", "linux"] {
            for requested in [None, Some(Toolchain::Native)] {
                let resolution = resolve(os, os, requested, &only(&[Toolchain::Native])).unwrap();
                assert_eq!(resolution.toolchain, Toolchain::Native);
                assert_eq!(resolution.fell_back_from, None);
            }
        }
    }

    #[test]
    fn windows_prefers_msvc() {
        let resolution = resolve("windows", "windows", None, &only(&[Toolchain::Msvc, Toolchain::Mingw])).unwrap();
        assert_eq!(resolution.toolchain, Toolchain::Msvc);
        assert_eq!(resolution.fell_back_from, None);
    }

    #[test]
    fn windows_falls_back_to_mingw_without_visual_studio() {
        let resolution = resolve("windows", "windows", None, &only(&[Toolchain::Mingw])).unwrap();
        assert_eq!(resolution.toolchain, Toolchain::Mingw);
        assert_eq!(resolution.fell_back_from, Some(Toolchain::Msvc));
    }

    #[test]
    fn windows_without_any_toolchain_is_an_error() {
        let err = resolve("windows", "windows", None, &only(&[])).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::NotFound);
    }

    #[test]
    fn windows_compiles_with_mingw_when_host_is_macos_or_linux() {
        for host in ["macos", "linux"] {
            let resolution = resolve(host, "windows", None, &only(&[Toolchain::Native, Toolchain::Mingw])).unwrap();
            assert_eq!(resolution.toolchain, Toolchain::Mingw);
            assert_eq!(resolution.fell_back_from, None);
        }
    }

    #[test]
    fn cross_compiling_to_windows_needs_mingw() {
        for host in ["macos", "linux"] {
            let err = resolve(host, "windows", None, &only(&[Toolchain::Native])).unwrap_err();
            assert_eq!(err.kind(), io::ErrorKind::NotFound);
        }
    }

    #[test]
    fn explicit_toolchain_never_falls_back() {
        let resolution = resolve("windows", "windows", Some(Toolchain::Msvc), &only(&[Toolchain::Msvc, Toolchain::Mingw])).unwrap();
        assert_eq!(resolution.toolchain, Toolchain::Msvc);
        assert_eq!(resolution.fell_back_from, None);

        // Mingw is there and the implicit choice would fall back to it, but msvc was asked for.
        let err = resolve("windows", "windows", Some(Toolchain::Msvc), &only(&[Toolchain::Mingw])).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::NotFound);
    }

    #[test]
    fn explicit_mingw_on_windows() {
        let resolution = resolve("windows", "windows", Some(Toolchain::Mingw), &only(&[Toolchain::Msvc, Toolchain::Mingw])).unwrap();
        assert_eq!(resolution.toolchain, Toolchain::Mingw);
        assert_eq!(resolution.fell_back_from, None);
    }

    #[test]
    fn rejects_a_toolchain_that_cannot_build_the_target() {
        for host in ["macos", "linux"] {
            for requested in [Toolchain::Msvc, Toolchain::Native] {
                let err = resolve(host, "windows", Some(requested), &only(&[Toolchain::Native, Toolchain::Mingw, Toolchain::Msvc])).unwrap_err();
                assert_eq!(err.kind(), io::ErrorKind::InvalidInput, "{requested} from {host}");
            }
        }
        let err = resolve("windows", "windows", Some(Toolchain::Native), &only(&[Toolchain::Native])).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidInput);
    }

    #[test]
    fn linux_and_macos_cannot_cross_compile() {
        for (host, target) in [("macos", "linux"), ("linux", "macos")] {
            for requested in [None, Some(Toolchain::Native)] {
                let err = resolve(host, target, requested, &only(&[Toolchain::Native])).unwrap_err();
                assert_eq!(err.kind(), io::ErrorKind::Unsupported, "{host}->{target}");
            }
        }
    }

    #[test]
    fn unknown_host_or_target() {
        for (host, target) in [("freebsd", "linux"), ("macos", "freebsd")] {
            let err = resolve(host, target, None, &only(&[Toolchain::Native, Toolchain::Mingw])).unwrap_err();
            assert_eq!(err.kind(), io::ErrorKind::Unsupported);
        }
    }

    #[test]
    fn native_sets_nothing() {
        for (host, target) in [("macos", "macos"), ("linux", "linux")] {
            assert_eq!(settings(host, target, Toolchain::Native), ToolchainSettings::default());
        }
    }

    #[test]
    fn msvc_sets_nothing_for_now() {
        assert_eq!(settings("windows", "windows", Toolchain::Msvc), ToolchainSettings::default());
    }

    #[test]
    fn mingw_cross_compile_matches_todays_build() {
        for (host, target) in [("macos", "windows"), ("linux", "windows")] {
            let settings = settings(host, target, Toolchain::Mingw);
            assert_eq!(settings, ToolchainSettings {
                generator: None,
                cmake_args: [
                    "-DCMAKE_SYSTEM_NAME=Windows",
                    "-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc",
                    "-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++",
                    "-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++ -static"
                ].to_vec(),
                vcpkg_triplet: Some("x64-mingw-static"),
                cargo_triple: Some("x86_64-pc-windows-gnu")
            });
        }
    }

    #[test]
    fn mingw_on_windows_is_not_a_cross_compile() {
        let settings = settings("windows", "windows", Toolchain::Mingw);
        assert_eq!(settings.generator, Some("MinGW Makefiles"));
        assert!(!settings.cmake_args.contains(&"-DCMAKE_SYSTEM_NAME=Windows"));
        assert!(settings.cmake_args.contains(&"-DCMAKE_C_COMPILER=gcc") && settings.cmake_args.contains(&"-DCMAKE_CXX_COMPILER=g++"));
    }

    #[test]
    fn every_mingw_build_links_statically_and_targets_gnu() {
        for host in ["macos", "linux", "windows"] {
            let settings = settings(host, "windows", Toolchain::Mingw);
            assert!(settings.cmake_args.contains(&"-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++ -static"));
            assert_eq!(settings.cargo_triple, Some("x86_64-pc-windows-gnu"));
            assert_eq!(settings.vcpkg_triplet, Some("x64-mingw-static"));
        }
    }
}
