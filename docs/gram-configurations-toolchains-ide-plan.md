# Plan — build configurations, toolchains, IDE projects (Tasks 0.8, 0.9 + a new one)

Status as of 2026-11-16: **done.** This is the plan as it was written on macOS before the work started (translated from Romanian); some details changed on the way. As built: phase 1 is Task 0.10 (`CLAUDE.md`, *Gram Build*), phase 2 is in `docs/gram-native-toolchains.md` (*As built*), phase 3 in `docs/gram-ide-projects.md`, phase 4 in `docs/gram-windows-visual-studio.md`. What's left is Task 0.11 in `.gram.kanban.md`.

## Context

Until now `gram build` does one kind of build (Debug, `BUILD_CONFIG` in `gram/gram-common/src/lang/cmake.rs`), with the compiler CMake picks by default on the host, and mingw wired only into the cross-compile branch (the `"windows"` arm in `cmake.rs`, `cargo_cross_triple` in `target.rs`). Debugging only worked once `CMAKE_BUILD_TYPE=Debug` was passed, and on Windows/WSL2 the choice of compiler has been a source of problems. The goal:

1. **Debug / Release / Final** — the groundwork for the packaging wave.
2. **A notion of toolchain** — picked by default from the host, overridden explicitly (`--toolchain`), with a fallback (Windows: MSVC → mingw).
3. **`gram ide xcode|vs`** — one IDE project for the whole game (C++ + Rust), building and debugging from the IDE.

The chosen order: configurations → toolchain (today's behaviour as the default) → Xcode → (later, on Windows) VS + MSVC + fallback. Only macOS is available now, so phases 1–3 must be fully testable here; phase 4 is written and tested on Windows.

**Splitting the work (mixed):** each step is marked **[User]** (something new to learn: the data model, the mapping, the generator) or **[Claude]** (plumbing: clap flags, paths, tests, `.vscode`, docs). Claude reviews after each [User] step, and we stop after each phase.

---

## Phase 1 — The Debug / Release / Final configurations

The semantics (chosen):
| | C++ | Rust | Engine |
|---|---|---|---|
| **Debug** | `-O0 -g` | the `dev` profile | editor (ImGui), log, validation layers |
| **Release** | optimized **+ symbols** | `--release` + `CARGO_PROFILE_RELEASE_DEBUG=true` | editor, log; no validation |
| **Final** | optimized, LTO, strip | `--release` + `CARGO_PROFILE_RELEASE_LTO=true`, `..._STRIP=true` | **no editor/ImGui**, no validation |

For cargo, the `CARGO_PROFILE_RELEASE_*` environment variables are used instead of a `[profile.final]`: no `Cargo.toml` has to be edited (profiles only count at a workspace's root), and the artifact's path already comes from `--message-format=json` (`cargo.rs`), so `target/release` works with no other change.

### Steps

1. **[User]** `gram-common/src/profile.rs`: `enum Profile { Debug, Release, Final }` (the name `Config` is already taken by `BuildConfig`/`config/`), `FromStr`/`Display`, and a mapping as data: `cmake_config()` (`"Debug"`/`"Release"`/`"Final"`), `cargo_args()` + `cargo_env()`. Pure functions, with unit tests.
2. **[Claude]** `--config debug|release|final` (default `debug`) on `BuildCommand` and `RunCommand`; `Profile` carried through `BuildConfig::build` → `ModuleConfig::build_target` → `ModuleBackend::compile` (its signature takes `profile`). In `cmake.rs`, `BUILD_CONFIG` goes away in favour of `profile.cmake_config()`; the build dir becomes `build-<target>-<profile>` (switching configurations no longer breaks the other one's objects).
3. **[Claude]** The output paths: `build/<project>/<target>/<profile>/` (and `build/<project>_<version>_<target>_<profile>/` with `--ci`), plus `ModuleConfig::run`. `.vscode/launch.json` / `tasks.json` and the tests in `gram/gram-cli/tests/` updated (`build.rs`, `run.rs`: one test with `--config release`, one with an unknown configuration).
4. **[User, with Claude for CMake]** `GramEngine/GramConfigurations.cmake`: declares the `Final` configuration to CMake — `CMAKE_CXX_FLAGS_FINAL`/`CMAKE_EXE_LINKER_FLAGS_FINAL` (= Release + LTO/strip), `CMAKE_CXX_FLAGS_RELEASE` with `-g`, `CMAKE_MAP_IMPORTED_CONFIG_FINAL=Release` (for vcpkg's dependencies), `CMAKE_CONFIGURATION_TYPES=Debug;Release;Final` for multi-config generators. It must be included before the first `project()`; gram passes it with `-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=…` (CMake ≥ 3.24, check the installed version). Remove the stray `set(GRAM_IMGUI_ENABLED)` from `GramEngine/CMakeLists.txt`.
5. **[User]** The configuration macros, following the convention in `Common.hpp`: CMake defines `GRAM_CONFIG_DEBUG`/`_RELEASE`/`_FINAL` through **generator expressions** (`$<$<CONFIG:Final>:GRAM_CONFIG_FINAL>`, `PUBLIC` on `GramEngine`) — required, so they also work in Xcode/VS, where the configuration is picked at build time, not at configure time. `Gram/Common.hpp` derives from them `GRAM_IMGUI_ENABLED` (not in Final), `GRAM_LOGGER_ENABLED` and a `GRAM_VULKAN_VALIDATION` (Debug only), which `VulkanRendererSpecification` reads (`Renderer/Common.hpp`, `m_RequiredValidationLayers`).
6. **[User]** The game in Final, without the editor: today the scene is only visible through `ViewportPanel`. Final needs a pass copying the scene's target into the swapchain (a `PresentPass`/blit, the last pass when ImGui is missing), and `main.cpp` fences its `Editor`/`GameUI` with `GRAM_IMGUI_ENABLED`. A separate step: Final can exist as a build before it shows anything on screen.

**Verification:** `cargo test --manifest-path gram/Cargo.toml`; `gram build --config debug|release|final`, each followed by `gram run --config …` (Release: the editor + a visibly higher FPS than Debug on the final scene, ~8 FPS as the reference; Final: the window without ImGui, step 6); F5 in VS Code still stops at a breakpoint.

---

## Phase 2 — The notion of toolchain (without changing today's results)

1. **[User]** `gram-common/src/toolchain.rs`: `enum Toolchain { Native, Mingw, Msvc }` (`Native` = what CMake picks on the host: Apple clang, GCC) and `fn resolve(host, target, requested: Option<Toolchain>, probe: &dyn Fn(&str) -> bool) -> io::Result<Toolchain>`:
   - explicit (`--toolchain mingw`) → no fallback, a clear error if it's missing;
   - implicit: target == host → `Native` (on Windows: `Msvc`, with a **fallback to `Mingw`** + a warning if `probe` doesn't find MSVC); Windows from Linux/macOS/WSL2 → `Mingw`; linux ↔ macos → an error (what `buildable_on_host` does today).
   - `probe` injected (in production: a `PATH` lookup, later `vswhere`) so it can be tested on macOS with fake probes.
2. **[User]** `struct ToolchainSettings { generator, cmake_args, vcpkg_triplet, cargo_triple, env }` from `(target, toolchain, profile)` — the only place that knows about `x86_64-w64-mingw32-g++`, `x64-mingw-static`, `-static-libgcc …`, `x86_64-pc-windows-gnu`. The `Msvc` arms return a "not implemented" error for now (phase 4).
3. **[Claude]** Moving the callers: the `"windows"` arm of `CMake::compile` and `cargo_cross_triple`/`buildable_on_host` in `target.rs` read `ToolchainSettings`; `--toolchain` on `build` (and on `ide`). The existing command tests must pass unchanged (that's the proof that the defaults = today's behaviour); table-style unit tests for `resolve` and `ToolchainSettings`.

**Verification:** `cargo test`; `gram build` and `gram build --ci` on macOS give the same artifacts as before (including mingw's `.exe`); `gram build --target windows --toolchain msvc` on macOS → an explicit error.

---

## Phase 3 — `gram ide xcode`

The idea (from `docs/gram-ide-projects.md`): gram generates a **CMake superproject** in `build/ide/xcode/` and configures it with `-G Xcode`; CMake writes the `.xcodeproj`.

1. **[Claude]** `commands/ide.rs`: `gram ide <xcode|vs> [--open] [--toolchain]` (`vs` refused on macOS). Configures with vcpkg's toolchain and a combined vcpkg manifest of every cmake module (extending `vcpkg::build_manifest`, which today combines one module's manifests + its references'), plus `GramConfigurations.cmake` from phase 1 → Xcode's scheme menu has Debug/Release/Final.
2. **[User]** The superproject's generator (`BuildConfig::ide_project()` → text, like the emitters in `schema/`):
   - `add_subdirectory` for each cmake module **not referenced** by another cmake module (today only `Game/Runtime`; `GramEngine` comes in through it — otherwise it would be added twice);
   - for each cargo module, an `add_custom_target(GameLogic …)` running `gram build --module GameLogic --config $<LOWER_CASE:$<CONFIG>>` (reusing the schemas' generation — `Runtime` includes `Components.generated.hpp` — the `libGameLogic.dylib` naming and cargo) and copying the library into `$<TARGET_FILE_DIR:Runtime>`; gram's path = `std::env::current_exe()` at generation time;
   - `add_dependencies(Runtime GameLogic)` for the executables loading it (for now: every cmake runtime of the same gram project);
   - a post-build on the executable: `shaders/` and `textures/` from `${CMAKE_BINARY_DIR}` next to it (the same list as `ASSET_DIRS` in `cmake.rs`), and `OUTPUT_NAME` = gram's name (`Game`).
3. **[Claude]** Xcode details: `CMAKE_XCODE_GENERATE_SCHEME ON`, `XCODE_SCHEME_WORKING_DIRECTORY` = the executable's directory, the executable as the default Run target; `--open` → `open build/ide/xcode/<project>.xcodeproj`.
4. **[Claude]** Tests: a command test checking only the generated text (no Xcode); an `#[ignore]`d one configuring with `-G Xcode`.

**Verification (by hand, in Xcode):** `gram ide xcode --open`; the GramEngine/Runtime/GameLogic hierarchy visible; Run in Debug with a breakpoint in `RayTracePass.cpp`; a change in `Game/Content/scripts`, Build on the `GameLogic` target while the game runs → a reload in the Log; the Release and Final schemes compile and start.

---

## Phase 4 — Windows (later, on the target machine)

- **[User/Claude]** `Toolchain::Msvc` in `ToolchainSettings`: the Visual Studio 18 2026 generator, the `x64-windows-static` triplet, `x86_64-pc-windows-msvc`, a consistent `/MT` CRT, the subsystem decided explicitly; detection with `vswhere`, a fallback to mingw with a warning; `gram build` from an ordinary terminal (the `vcvars` environment from `vswhere`).
- **[Claude]** `gram ide vs`: the same superproject, plus `VS_STARTUP_PROJECT` and `VS_DEBUGGER_WORKING_DIRECTORY`.
- The checklist is already in `docs/gram-native-toolchains.md` ("Suggested order").

## Documentation (Claude, at the end of each phase)

`.gram.kanban.md` (a new Task 0.10 for the configurations; 0.8/0.9 updated), `docs/gram-ide-projects.md`, `docs/gram-native-toolchains.md`, `README.md` (the new flags), the Gram Build section of `CLAUDE.md`.

## Main files

- `gram/gram-common/src/lang/cmake.rs`, `lang/cargo.rs`, `lang/mod.rs` (`ModuleBackend::compile`), `lang/vcpkg.rs`
- `gram/gram-common/src/config/mod.rs` (`build`), `config/module.rs` (`build_target`, `run`), `target.rs`
- new: `profile.rs`, `toolchain.rs`, `commands/ide.rs`
- `gram/gram-cli/tests/` (`build.rs`, `run.rs`, new `ide.rs`)
- `GramEngine/CMakeLists.txt`, new `GramEngine/GramConfigurations.cmake`, `GramEngine/include/Gram/Common.hpp`, `Renderer/Common.hpp`
- `Game/Runtime/src/main.cpp`, `.vscode/launch.json`, `.vscode/tasks.json`
