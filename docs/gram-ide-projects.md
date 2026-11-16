# Notes — IDE projects and debugging (Task 0.9)

Status as of 2026-11-16: **debugging in VS Code is done; `gram ide xcode` is done (built with `xcodebuild` in Debug and Release); `gram ide vs` is done and verified in Visual Studio (phase 4, `docs/gram-windows-visual-studio.md`).**

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

### As built (Visual Studio, 2026-11-16)

`gram ide vs [--open]` runs the same code with the `Visual Studio 18 2026` generator and msvc's settings (`-A x64`, the static CRT, `x64-windows-static`) into `build/ide/vs/gram-engine.slnx`. Verified in Visual Studio: F5 starts `Game` (`VS_STARTUP_PROJECT`), breakpoints hit. What differed from Xcode (the `PATH`'s escaping, `.slnx`, FXC compiling the shown `.hlsl` files) and why hot reload goes through the editor's Play button there (Visual Studio doesn't build while it debugs): `docs/gram-windows-visual-studio.md`.

## Breakpoints in Rust (GameLogic)

- **Visual Studio**: set them in the `.rs` file itself; its debugger accepts a breakpoint in any text file and finds the line in the `.pdb` rustc writes.
- **Xcode**: refuses breakpoints in files it doesn't compile itself (a `.rs` is only shown). LLDB underneath knows Rust, so set them from the LLDB console while the game runs: `breakpoint set --file greeter.rs --line 17` or `breakpoint set --name greet`; they stay pending until `libGameLogic.dylib` loads (and come back after a Play reload). Variables show as raw structures (no `rust-lldb` formatters). Untried as of 2026-11-16.
- **VS Code** (CodeLLDB, `cppvsdbg` on Windows): only on a process it debugs. Either launch `Game (<platform>)`, or start the game anywhere else and use `Attach to Game (<platform>)` (`.vscode/launch.json`): the editor is part of the `Game` process, so Play / Stop work the same. Verified on macOS 2026-11-16: `gram run --module Runtime` in a terminal, then *Attach to Game (macOS)*, and a breakpoint in a Rust system hits when it runs. A process takes one debugger at a time: to start it from Xcode without LLDB, uncheck *Debug executable* in the scheme (Product › Scheme › Edit Scheme… › Run › Info); ⌃⌘R is *Run Without Building* and still debugs.

## Related

- `docs/gram-native-toolchains.md` — native compilers per platform (Task 0.8).
- `README.md`, *Setup* — bootstrapping `gram` with cargo, which the `gram build` VS Code task relies on.
