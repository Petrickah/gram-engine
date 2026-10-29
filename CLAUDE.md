# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. It is self-contained: this repo is meant to be cloned standalone on dev machines (Windows, macOS) that don't have the author's Obsidian vault or the `Projects` monorepo it's normally a submodule of — don't assume either is available. Current task-level progress lives in `.gram.kanban.md`; check it first.

## What this is

Gram — a personal game engine (Rust + C++), started from rediscovered passion, not a portfolio obligation. No deadline. Structured in "waves": each wave produces one working artifact plus a devlog post (`petretiberiu.dev`).

## Waves

- **Wave 0 — Gram Build** (done): a pure-Rust CLI build tool — no C++, no runtime deps. Full command set: `gram init`/`build`/`add`/`remove`/`run`, driving a `Gram.ron` project file (serde + `ron-rs`, a format reused later for asset definitions). A `cargo` module kind delegates straight to real `cargo`; a `cmake` module kind scaffolds a C++ project with FetchContent/vcpkg dependency resolution, for when Wave 1 needs it. The project is self-hosting (`gram` builds itself via its own `Gram.ron`). Details: `docs/wave-0-*.md`.
- **Wave 1 — Hello Triangle**: window + Vulkan + triangle + ImGui, C++/Rust interop. C++ owns `main()`, the renderer and **all state** (entities, components, IDs; never reloaded). Rust is a single stateless `cdylib` (`gram-hot`) holding only gameplay logic, loaded/reloaded via `LoadLibrary`/`dlopen` (copy-on-load) and called through plain `extern "C"` with a `HostApi` function table; shared data is `#[repr(C)]` in host memory, reached from Rust through IDs (`Handle`/`Reference`). No `bevy_ecs`, no `gram-core`, no `cxx`/Corrosion (adopted 2026-10-28, replacing the earlier `gram-core` static + `gram-hot` split). Components are described in `.ron` schemas from which `gram build` generates the Rust struct, the C++ struct and a descriptor table (like Anvil's `.mold`); a schema change needs a rebuild + restart, a Rust logic change only a reload. Proof of concept: a `Greeting` component with one text field, edited in an ImGui inspector, surviving a reload — no sphere in Wave 1. Details: `docs/wave-1-rust-gameplay-design.md`.
- **Wave 2 — Ray Tracer**: Vulkan compute shader pipeline; spheres first owned by C++, then driven by Rust entities (position/color/radius).
- **Wave 3+**: raster pipeline, post-processing, asset manager, world serialization — noted, unplanned.

## Methodology — who writes what

- **User writes**: anything that's a new skill being learned for the first time (a CLI with subcommands the first time, Vulkan code following the official tutorial, ECS entity/component/system definitions) — the parts with real learning value.
- **Claude Code writes**: scaffolding (CLI boilerplate, build system wiring, Rust↔C++ interop glue, CMake setup) — plumbing without learning value, once a pattern is already familiar.
- Rule of thumb: first time doing something new → user writes it. Something done many times before (Cargo.toml config, adding a dependency) → delegate to Claude Code.

## Commands

The `gram` crates live in a cargo workspace at `gram/Cargo.toml` (`gram-cli`, package `gram`, plus the `gram-common` library), not the repo root, so plain `cargo` needs `--manifest-path` (or `cd gram` first):

- Build: `cargo build --manifest-path gram/Cargo.toml`
- Run: `cargo run --manifest-path gram/Cargo.toml -- <args>` (e.g. `-- --help`, `-- init hello-world`)
- Test: `cargo test --manifest-path gram/Cargo.toml` — command-level tests in `gram/gram-cli/tests/` (one file per subcommand plus `nested.rs`; read `tests/support/mod.rs` first). They run the real binary in a temp sandbox, kept on failure. The `build`/`run` tests need cmake and a C++ compiler; one test needs crates.io and is `#[ignore]`d.
- Self-hosted (once bootstrapped): from the repo root, `gram/target/debug/gram build` (or `build --ci`, `run`) builds the whole tree from the checked-in root `Gram.ron` — `gram` itself, `GramEngine` and the game. The tool's own artifact lands as `build/<project>/<target>/gram`.

## Architecture (current)

The repo is a Gram project tree. Root `Gram.ron` (`gram-engine`, `main: "GramRuntime"`) holds three modules: `gram` (a nested project → `gram/Gram.ron`: `gram-cli` runtime + `gram-common` library), `GramEngine` (cmake, `STATIC` library) and `Game` (a nested project → `Game/Gram.ron`: `GramRuntime`, cmake runtime, `Module(name: "GramEngine")`).

### Gram Build (`gram/`)

Rust, edition 2024, `clap` (derive) + `serde`/`ron-rs`. `gram-cli/src/main.rs` only parses and prints outcomes; everything else is in `gram-common/src/`:

- `commands/` — clap structs (`Cli`, `InitCommand`, `BuildCommand`, `AddCommand`, `RemoveCommand`, `RunCommand`) plus thin functions calling `BuildConfig` (see `README.md` for the flag reference).
- `config/mod.rs` — `BuildConfig`, the `Gram.ron` project: `open`/`write` (nested `Gram.ron` files are loaded into one flat module list and split back on write), module add/remove, `build`/`run` over modules, `resolve`/`module_refs` (→ `ResolvedModule`), `artifact_name`, and `add_dependency` returning an `AddOutcome` (`ModuleCreated`/`TargetsAdded`/`DependencyAdded`/`AlreadyPresent`/`NothingToDo`). The CLI decides what to print; the core only reports.
- `config/module.rs` — `ModuleConfig` (`name`, `lang`: `"cargo"`|`"cmake"`|`"gram"`, `kind`: `"runtime"`|`"library"`, `src`, `deps: Vec<DependencySpec>`, `targets`: `"linux"`|`"windows"`|`"macos"`, plus a `#[serde(skip)]` field remembering its project file), the language-independent build steps and `dir()` (the parent of `src`).
- `lang/mod.rs` — the `ModuleBackend` trait (`manifest_file`, `scaffold`, `compile`, `artifact_naming`, `add_dependency`, `rm_dependency`, `add_module_ref`, `rust_analyzer_manifest`), `backend_for(lang)` — the only place that matches on the `lang` string —, `DependencySpec` (`Registry{name}`/`Git{name,url,tag}`/`Vcpkg{name}`/`Module{name}`, serialized as-is into `Gram.ron`) and `ResolvedModule` (computed at runtime, never serialized: `dir` relative to the project root, `rel_dir` relative to the consumer).
- `lang/cargo.rs`, `lang/cmake.rs`, `lang/gram.rs` — the three zero-sized backends. `lang/vcpkg.rs` — helpers used by cmake (manifest read/write, merging referenced modules' manifests); vcpkg is a dependency source, not a module language.
- `target.rs` — `SUPPORTED_TARGETS`, `validate_targets`, `buildable_on_host`, `cargo_cross_triple`. `directory.rs` — `Directory` file I/O. `vscode.rs` — `write_vscode_settings` (merges into an existing `settings.json`).

gram's cwd is always the project root: `cargo add --path` takes a root-relative path (`ResolvedModule::dir`), cmake's `add_subdirectory` a consumer-relative one (`rel_dir`).

Module language behavior:
- `cargo` modules delegate straight to real `cargo` (`init`/`add`/`rm`/`build`), including cross-compiling to `x86_64-pc-windows-gnu` via the vendored `.cargo/config.toml` (mingw linker, static CRT). `compile` reads the artifact path from `cargo build --message-format=json-render-diagnostics` instead of guessing it, so workspaces and multi-bin packages work. Only registry dependencies are accepted.
- `cmake` modules are scaffolded by `gram` (`CMakeLists.txt` + `src/main.cpp`, or for a library `include/<name>/<name>.hpp` + `src/<name>.cpp`) and built by invoking `cmake` directly (`--fresh`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`). A managed region in `CMakeLists.txt` (`# --- gram dependencies ---` markers) lets `add`/`remove` edit `FetchContent_Declare` blocks (`--url`/`--tag`) or vcpkg `find_package` calls (`--vcpkg`, needs `VCPKG_ROOT`; the generated `find_package`/`target_link_libraries` are a heuristic guess and sometimes need a manual fix — e.g. vcpkg's `vulkan` port resolves via CMake's built-in `FindVulkan` module) as text, without a real CMake parser. Windows cross-compile sets `CMAKE_SYSTEM_NAME=Windows` + the mingw compilers, statically linking the mingw runtime (`-static-libgcc -static-libstdc++ -static`) so the `.exe` runs standalone; a vcpkg module additionally gets `-DVCPKG_TARGET_TRIPLET=x64-mingw-static` (vcpkg would otherwise pick the MSVC-based `x64-windows`). vcpkg only reads the top-level manifest, so for a module with `--module-ref`s gram merges the referenced modules' `vcpkg.json` into `<build dir>/vcpkg-manifest/` and passes `VCPKG_MANIFEST_DIR`. Libraries are found whether they're static or shared and copied under their real file name. After a build, `<build dir>/shaders/` is copied next to the executable — a stopgap until modules can declare extra outputs.
- `gram` modules (`lang: "gram"`, `src: "<dir>/Gram.ron"`) are nested Gram projects: they compile nothing themselves, take no dependencies and can't be run. Module names are unique across the whole tree. `gram add --parent <gram module>` creates a module inside one.
- Either cargo or cmake supports `--module-ref`: one module depending on another (same `lang`; the referenced module must be `kind: "library"`) — a cargo path dependency, or a cmake `add_subdirectory` + `target_link_libraries` pair. Cross-language references aren't supported. Duplicate dependencies are detected by name, whatever the source.

Builds land in `build/<project>/<target>/` (plain `gram build`) or `build/<project>_<version>_<target>/` (`--ci`, one per buildable target: the host OS, plus `windows` via mingw from anywhere; `linux` ↔ `macos` is skipped with a warning). Every module of the tree lands in the root project's directory. A project's main runtime module is copied under the project's name (`Game`, `gram`), any other module under its own. `gram run` executes an already-built `runtime` module's binary directly — it doesn't trigger a build.

### GramEngine (C++20, Vulkan 1.3)

`GramEngine/include/Gram/` + `src/Gram/`, built as two `STATIC` targets: `GramRenderer` (the Vulkan components, precompiled header `Gram/Renderer/Common.hpp`) and `GramEngine` (application, logger, platform; links `GramRenderer` `PUBLIC`, precompiled header `Gram/Common.hpp`), so anything linking `GramEngine` gets the headers, the libraries and the PCH. Headers forward-declare where they can.

- `EntryPoint.hpp` — `main`/`WinMain` call the game's `Gram::CreateApplication(argc, argv)` (returns `std::shared_ptr<Application>`), then `Run()`.
- `Application.hpp` — the `Run()` contract. `Application/GLFWApplication` — window + main loop, a `LayerOwner`; its constructor creates and registers the `VulkanRenderer` from the `GLFWApplicationSpecification` (window, title, workspace, shader program name).
- `Layer.hpp` — `Layer<Owner>` / `LayerOwner<Owner>`: one layer per type (guarded in `RegisterLayer<T>(args...)`, constrained with C++20 `requires std::derived_from<…, Layer<GramObject>>`), started in registration order, stopped in reverse. Two owners today: `GLFWApplication` (holds `VulkanRenderer`) and `VulkanImGui` (holds `ImGuiApplicationLayer`s — the game's UI windows).
- `Renderer/` — `VulkanRenderer` coordinates RAII components that leave themselves invalid (`IsValid()`) instead of crashing: `VulkanInstance` (+ debug messenger), `VulkanSurface` (the only file that knows GLFW), `VulkanDevice` (device scoring in tiers, queue families, extensions, graphics/present queues), `VulkanSwapchain` (`Recreate()` on resize/minimize), `VulkanShader` (move-only, `vk::UniqueShaderModule`), `VulkanPipeline` (dynamic rendering), `VulkanFrame` (command buffers, two frames in flight), `VulkanImGui` (docking + multi-viewport). `UniqueObject` (no copy/move) and `MoveObject` (move-only) mark ownership. Built with `VULKAN_HPP_NO_CONSTRUCTORS` (designated initializers) and `VULKAN_HPP_NO_EXCEPTIONS`.
- `Logging/GramLogger.hpp` — spdlog-based: `GramInfo`/`GramWarning`/`GramError` (+ `GramVulkan*`), check macros that return on failure (`GramCheck` → `false`, `GramCheckVoid`, `GramVulkanCheck`/`CheckVoid`/`CheckResult`), and list markers (`GramVulkanListSelected`/`Available`/`Added`/`Missing`, `GramVulkanListItem`). `GramCheck(x)` reads like an assert: it fails when `x` is false.
- `Platform.hpp` — `GetExecutableDirectory()` (`GetModuleFileNameW` / `/proc/self/exe` / `_NSGetExecutablePath`; `argv[0]` is wrong under WSL interop).
- `Common.hpp` — platform and feature macros: Apple/MoltenVK-only code is fenced with `GRAM_PLATFORM_APPLE`; capabilities such as `GRAM_GEOMETRY_SHADERS` are derived from the platform here, and renderer code tests the feature macro, not the platform.
- Shaders: `GramShaders.cmake` provides `gram_add_hlsl_shader(<target> <name>)`, compiling `shaders/<name>.hlsl` (entry points `VSMain`/`PSMain`) with the Vulkan SDK's `dxc` into `<top-level build dir>/shaders/<name>.{vert,frag}.spv`. The shaders belong to the game (`Game/GramRuntime/shaders/`); the engine only owns the tooling.
- Dependencies (root `Gram.ron`): vcpkg `vulkan` (headers only off Windows — the loader comes from the system/Vulkan SDK), `glfw3`, `glm`, `imgui` (features `glfw-binding`, `vulkan-binding`, `docking-experimental`), spdlog via FetchContent.

`Game/GramRuntime/src/main.cpp` is a thin launcher: `RuntimeApplication : GLFWApplication` registers its UI layer, and `CreateApplication` only fills the application spec.

No C++/Rust interop yet. In Wave 1 it needs no build-system bridge: the C++ executable only `dlopen`s/`LoadLibrary`s the Rust `cdylib`, so `gram` needs `crate-type = ["cdylib"]` support, copying the library next to the executable, and later the `.ron` schema → Rust/C++ code generation step.

## Non-goals (current)

- No multiplayer/networking, no visual editor, no emulators/plugins — noted ideas, not now.
- No performance/optimization focus — functional correctness first.
- **Cross-compilation is scoped to `--ci` builds only** (added 2026-09-09): a `gram build --ci` iterates every target in `ModuleConfig.targets` (`linux`/`windows`/`macos`, skipping what the host can't build: `windows` cross-compiles via mingw from anywhere, `linux` ↔ `macos` doesn't); a plain `gram build` only compiles for the host it's run on, and `--target` builds a single target. Wave 1's native deps (Vulkan, GLFW, ImGui) cross-compile to Windows through vcpkg's `x64-mingw-static` triplet. In practice: develop on macOS, `gram build --ci` there, run the `.exe` natively on Windows and build Linux in WSL2.
- **No CI/CD until an explicit "Personal Builds" stage.** Planned design when that starts: GitHub Actions (free hosted runners) run classic Rust CI (`cargo build`/`test`/`clippy`) on every push; only on success, one step uses `tailscale/github-action` (ephemeral, ACL-scoped tailnet node, no exposure) to reach the private network and either mirror to Gitea (triggers the existing Jenkins webhook) or call Jenkins directly. A permanent self-hosted GitHub Actions runner was considered and rejected — it works (outbound-poll only, no port to open) but needs an ongoing-maintained process; the hosted-runner + ephemeral-Tailscale-hop design needs nothing running when idle.

## Repo model

Private GitHub repo (`Petrickah/gram-engine`) is the sole origin for now — deliberately not the Gitea-first convention used elsewhere in the author's `Projects` monorepo, chosen for durable code history and eventual Patreon early-access (repo-level separation when that happens, not a branch split). Gitea gets added later only as an automatic pull-mirror of GitHub, once "Personal Builds" actually starts — not before.
