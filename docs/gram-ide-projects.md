# Notes — IDE projects and debugging (Task 0.9)

Status as of 2026-11-15: **debugging in VS Code is done; `gram ide xcode` is done (built with `xcodebuild` in Debug and Release); Visual Studio (`gram ide vs`) is written but untested, for phase 4 on Windows.**

## Debugging in VS Code (done)

Why breakpoints never hit on macOS/Linux: the CMake modules were configured with an **empty `CMAKE_BUILD_TYPE`**. With a single-config generator (Unix Makefiles, which is what CMake picks on Linux and macOS) that means no `-g` and no `-O0` — `main.cpp` was compiled with `-std=gnu++20` and nothing else, so there was nothing for a debugger to map breakpoints to. The multi-config Visual Studio generator on Windows builds `Debug` by default, which is why it never showed there.

What changed:

- `gram` now passes `-DCMAKE_BUILD_TYPE=Debug` when it configures a cmake module (since 2026-11-14 the build type comes from `gram build --config`, `Profile::cmake_config()` in `gram/gram-common/src/profile.rs`, also used for `cmake --build --config` and the multi-config artifact folder; `debug` is the default). Cargo modules were already debug builds.
- A side effect worth knowing: with a build type set, vcpkg's toolchain links the `debug/` libraries of the dependencies instead of the release ones.
- `.vscode/launch.json` is one file for every machine, with one configuration per target — `Game (Linux)`, `Game (macOS)`, `Game (Windows)` — instead of editing paths per platform. Linux and macOS use CodeLLDB (`vadimcn.vscode-lldb`); Windows uses the C/C++ extension's `cppvsdbg` (`ms-vscode.cpptools`), which reads the `.pdb` that stays in `Game/Runtime/build-windows/Debug/` (its path is embedded in the `.exe`, so it isn't copied next to it). The two `gram` (cargo) configurations are kept for debugging the tool itself.
- `.vscode/tasks.json` has the `gram build` task every Game configuration runs first (`preLaunchTask`), so F5 builds the whole tree for the host and then launches. It calls `gram/target/debug/gram`, not the copy under `build/`, which is the file `gram build` replaces.
- `.vscode/extensions.json` recommends the three extensions.

## IDE projects

### As built (Xcode, 2026-11-15)

`gram ide xcode` writes `build/ide/xcode/CMakeLists.txt` (`config/ide.rs`, `superproject`) and configures it with `-G Xcode` into the same directory: `gram-engine.xcodeproj`. What's in it:

- `add_subdirectory` of the cmake modules no other cmake module references (`Game/Runtime`; `GramEngine` comes in through it).
- `GameLogic`, a custom target running `gram build --module GameLogic --config $<LOWER_CASE:$<CONFIG>>` from the root: the schemas, cargo and the library's name are gram's, not reimplemented in CMake. It's the path of the gram that generated the project, run through `cmake -E env` with the `PATH` `gram ide` was run with: Xcode opened from the Finder or `open` doesn't get the shell's `PATH`, so gram wouldn't find `cargo` ("Command PhaseScriptExecution failed"). Run `gram ide` again after changing the `PATH`.
- `Runtime` named `Game` and output where `gram build` puts it (`build/gram-engine/macos/<config>/`), next to `libGameLogic.dylib`: building `GameLogic` in Xcode while the game runs reloads it. `add_dependencies(Runtime GameLogic)`, since gram also generates `Components.generated.hpp`.
- What's shown next to what's compiled (`ide_sources`, appended to the superproject): every cmake module's `.hpp`/`.h`, each on the target compiling the sources next to it (`gram_ide_headers`, a CMake function in the superproject: the one target of the module's directory with a source under the header's folder, `include/` read as `src/`, else the module's own target — so GramEngine's headers split between GramRenderer, GramScripting and GramEngine), and the `.rs`/`.ron`/`.toml`/`.hlsl` files of each cargo `dynamic` module (on its custom target) and of the cargo modules it references (`gram-core`, on a target holding only files). Globbed by CMake with `CONFIGURE_DEPENDS`, so a new file appears at the next build; `target/` and `build*/` left out, folders kept with `source_group(TREE ...)`. Xcode edits them but has no Rust highlighting or completion: rust-analyzer stays in VS Code.
- The shaders and textures follow the executable through `gram_copy_next_to` in `GramShaders.cmake`/`GramTextures.cmake`: CMake refuses a `POST_BUILD` command on a target from another directory, so the superproject can't do it.
- Configure: the configurations (`GramConfigurations.cmake`, so the scheme menu has Debug/Release/Final), vcpkg with the merged manifest, `CMAKE_OSX_ARCHITECTURES` = the host's (Xcode would build universal binaries in Release, which vcpkg's arm64 libraries can't link), `CMAKE_XCODE_GENERATE_SCHEME`, and `DEVELOPER_DIR=/Applications/Xcode.app/...` when `xcode-select` points at the Command Line Tools.

### Planned shape (kept for Visual Studio)

### Why there is no single project today

`gram` builds each module in its own CMake build directory (`GramEngine/build-<target>-<config>/`, `Game/Runtime/build-<target>-<config>/`) and Rust modules through cargo. CMake already writes a Visual Studio `.sln` or an Xcode `.xcodeproj` for each of those, so there is nothing to generate by hand — but none of them holds the whole game, and none of them builds the Rust part.

### Proposed shape

- A **superproject CMake file generated by `gram`** (for example `build/ide/CMakeLists.txt`): one `add_subdirectory()` per cmake module, one `add_custom_target` per cargo module that runs `cargo build`, wired as dependencies of the executable. Opened with `-G Xcode` or `-G "Visual Studio 18 2026"`, it gives the whole hierarchy in one solution/project, and Build/Run in the IDE builds the Rust side too.
- A command for it (for example `gram ide --ide xcode|vs`), separate from `gram build`.
- Alternatives that need no generated project: Visual Studio's *Open Folder* on a `CMakeLists.txt`, and VS Code (done above).
- Xcode and Visual Studio are both **multi-config** generators, so the artifact lookup under `<build dir>/Debug/` added for MSVC applies to Xcode as well.
- Debugging in Xcode needs a scheme whose run target is the game executable and a working directory next to the shaders; CMake's Xcode generator can set a startup scheme, to check.

### Order

1. Xcode on macOS first (as planned).
2. Visual Studio afterwards.
3. Make `gram build` work from an ordinary terminal on Windows, not only from *Developer PowerShell* — see `docs/gram-native-toolchains.md` (locating the Visual Studio install with `vswhere`).

## Related

- `docs/gram-native-toolchains.md` — native compilers per platform (Task 0.8).
- `README.md`, *Setup* — bootstrapping `gram` with cargo, which the `gram build` VS Code task relies on.
