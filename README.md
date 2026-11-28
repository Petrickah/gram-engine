# Gram Engine

## Overview

Personal game engine project (C++ + Rust), started from rediscovered passion rather than portfolio obligation. Structured in "waves", each producing a working artifact plus a devlog post on `petretiberiu.dev`.

- **Wave 0 — Gram Build** (done): `gram`, a pure-Rust CLI build tool that orchestrates `cargo`, multi-file `cmake` and nested `gram` modules, and builds this whole repo, itself included. Devlog: [Episode 0](https://petretiberiu.dev/posts/gram-engine-00-gram-build/).
- **Wave 1 — Hello Triangle + Rust gameplay** (done): Vulkan 1.3 renderer, ImGui, C++-owned world and hot-reloaded Rust systems/components. Devlog: [Episode 1](https://petretiberiu.dev/posts/gram-engine-01-hello-triangle/).
- **Wave 2 — Ray Tracer** (closed): compute-shader path tracing with Rust-driven sphere entities, pass schemas, BVH, GPU profiler and FSR 1. Episodes: [2](https://petretiberiu.dev/posts/gram-engine-02-ray-tracing-in-one-weekend-on-the-gpu/) and [2.5](https://petretiberiu.dev/posts/gram-engine-02-5-optimizing-for-real-hardware/).
- **Wave 3 — Triangles and Blender scenes** (planned): Möller–Trumbore, OBJ/MTL import, triangle BVH and unified material, validated against the existing Blender scene.
- **Wave 4 — Raster pipeline** (later): G-buffer, shadows and lighting; keep unplanned until Wave 3 is complete.

Current task-level progress lives in `.gram.kanban.md`; architecture and design decisions in `CLAUDE.md` and `docs/`. To change Gram Build itself, start with [docs/gram-architecture.md](docs/gram-architecture.md), the developer guide: data model, command flows, backends, how to add a language or a dependency source, tests, and known limits.

## Try it

A preview build is on the Releases page: [v0.1.0](https://github.com/Petrickah/gram-engine/releases/tag/v0.1.0), for macOS (Apple Silicon, macOS 26+) and Windows (x64), each in Release and Debug. Nothing to install; each zip's `README.txt` says how to run it. To build from source, see [Setup](#setup).

## How this is built

I build Gram Engine with Claude Code as a pair programmer. Most commits carry a `Co-Authored-By: Claude` trailer: it marks commits Claude took part in (code, tests, docs, or just making the commit), not how much of the code is Claude's. I usually asked Claude to commit, for fast and concrete descriptions of what the change does, even when it didn't modify any code.

Who did what, by area:

- **Vulkan renderer** — mine, following the Khronos tutorial: instance, devices, surface, swapchain, images, buffers, pipelines, shader loading, the core render pass.
- **Ray tracer** — the base algorithm is mine, following *Ray Tracing in One Weekend*, with Claude's guidance; the triangle intersection is mine.
- **C++/Rust interop** — my design; Claude implemented most of it.
- **Profiler** — the GPU timing is mine; the editor panel is Claude's.
- **Logging** — mine, reviewed by Claude.
- **Gram Build** — mostly Claude's; I wrote the basic Rust.
- **Editor, render targets, benchmarks** — Claude's, under my direction.
- **Documentation** — `docs/` and `CLAUDE.md` are written by Claude, from research on the codebase. I let Claude keep its own memory there.

**How we work:** the design decisions and which features to build are mine; I brainstorm them with Claude, but the final call is mine, and I review every change before it lands. When I'm stuck, Claude either changes the code directly or gives me a snippet and explains how to implement it. On complex tasks Claude works as a tutor, the senior engineer if you prefer. The per-task split is recorded in `.gram.kanban.md` ("user", "Claude reviewing", …).

## Repository layout

```
Gram.ron              root project: gram, GramEngine, Game
gram/                 Gram Build (nested gram project: gram-cli + gram-common, a cargo workspace)
GramEngine/           the engine, a static C++ library (Vulkan, GLFW, ImGui, spdlog)
Game/                 nested gram project for the game
  Runtime/            the game's executable: a thin C++ launcher over GramEngine
  Content/            what the game is made of: Rust scripts (built as libGameLogic), shaders
docs/                 design notes per wave
```

## Setup

- Rust toolchain (stable, 2024 edition), plus the `x86_64-pc-windows-gnu` target for Windows cross-builds
- CMake and a C++20 compiler; `mingw-w64` for Windows cross-builds (from Linux or macOS)
- vcpkg, with `VCPKG_ROOT` set
- Vulkan SDK (validation layers, `dxc` for the HLSL shaders; MoltenVK on macOS). The Vulkan loader the game links comes from vcpkg (built with xcb/xlib support on Linux, see `GramEngine/vcpkg.json`); it still finds the GPU drivers through the system's ICD files.
- Linux system packages for vcpkg's GLFW and ImGui builds (verified on Ubuntu under WSL2, not a complete list for a bare machine): `libgl-dev` (the OpenGL headers `glfw3.h` includes), `libx11-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxcursor-dev`, `libxi-dev`, `libwayland-dev`, `libxkbcommon-dev`, and `autoconf-archive` for vcpkg's xcb ports (a full list for a fresh Ubuntu 24.04: *SteamOS* in `docs/gram-native-toolchains.md`)

Bootstrap `gram` once with cargo, then let it build everything (itself included):

```
cargo build --manifest-path gram/Cargo.toml
gram/target/debug/gram build          # host OS
gram/target/debug/gram build --ci     # every declared target the host can build
gram/target/debug/gram run            # runs the game (the root project's main module)
```

Tests: `cargo test --manifest-path gram/Cargo.toml` (command-level tests in `gram/gram-cli/tests/`; the `build`/`run` ones need cmake and a C++ compiler).

## Usage

```
gram init <name> [--lang cargo|cmake] [--kind runtime|library|dynamic] [--target linux,windows,macos] [--git]
gram build [--path <dir>] [--ci] [--target linux|windows|macos] [--config debug|release|final] [--toolchain native|mingw|msvc] [--module <name>]
gram add [<dependency>] [--module <name>] [--lang cargo|cmake|gram] [--parent <gram module>] [--kind runtime|library|dynamic] [--target linux,windows,macos] [--url <git-url> --tag <tag>] [--vcpkg] [--module-ref]
gram remove [<dependency>] [--module <name>] [--purge]
gram run [--module <name>] [--target linux|windows|macos] [-- <args>]
gram ide xcode|vs [--open] [--generate-only]
gram deploy [--device <name>] [--config debug|release|final] [--module <name>] [--no-run] [--direct] [--debug] [--dry-run]
gram benchmark [<recording.csv or folder>...] [--config debug|release|final] [--target linux|windows|macos] [--skip <rows>] [--zero] [--output <png>] [--show]
```

- `gram init` scaffolds `<name>/Gram.ron` plus the initial module (`--lang cargo` delegates to a real Cargo crate; `--lang cmake` scaffolds a multi-file `src/` + `include/<name>/` CMake project). That module also becomes the project's `main`, the default for `gram run`. `--target` sets which platforms the module declares (`linux`/`windows`/`macos`); omit it to default to just the host OS.
- `gram build` compiles every module for the host OS into `build/<project>/<target>/<config>/`. `--config debug|release|final` (default `debug`) picks the configuration: debug (no optimization, symbols), release (optimized, with symbols) or final (optimized, LTO, stripped); cmake modules get one build directory per target and configuration (`build-<target>-<config>`). `--target` builds a single target instead, and warns (doesn't fail) if that target isn't declared in a module's `targets`. `--ci` builds every declared target the host can build (its own OS, plus `windows` via mingw from anywhere; `linux` ↔ `macos` is skipped with a warning) into `build/<project>_<version>_<target>_<config>/`, and can't be combined with `--target`. `--toolchain native|mingw|msvc` picks the compiler toolchain instead of the host's default (the system compiler on linux/macos; for Windows mingw from linux/macos, msvc on Windows falling back to mingw with a warning when it isn't found); an explicit one never falls back, and it can't be combined with `--ci`. `--module <name>` builds only that module (for a gram module, everything inside it), e.g. `gram build --module GameLogic` to rebuild the game's Rust logic while the game runs; what it references is still compiled by cargo or cmake themselves. Every module of the tree, nested ones included, lands in the root project's directory; a project's main runtime module is copied under the project's name (here `Game` and `gram`), any other module under its own. A cmake module's compiled `shaders/` are copied next to its executable.
- `gram add`/`gram remove` create/remove a module (`--module`, upserted if missing) and/or a dependency on it. Adding a dependency a module already has (by name, whatever the source) only prints a warning. `--target` on `gram add` sets the initial target list of a new module, or appends any not-yet-declared targets to an existing one. Dependencies are recorded in `Gram.ron` by origin: `Registry(...)`, `Git(name, url, tag)`, `Vcpkg(...)` or `Module(...)`.
  - `cargo` modules delegate straight to `cargo add`/`cargo rm`, and take plain registry dependencies only.
  - `--kind` picks what a new module produces: `runtime` (an executable), `library` (linked into another module: a cmake library, a cargo rlib) or `dynamic` (cargo only: a `cdylib` the executable loads at runtime, copied by `gram build` next to it, named after the gram module rather than the crate: module `GameLogic` → `libGameLogic.dylib`).
  - `cmake` modules resolve a dependency either via CMake FetchContent (`--url`/`--tag`, for any git-clonable CMake project) or a vcpkg manifest (`--vcpkg`, for anything with a maintained vcpkg port). Either way, `gram` only wires up dependency *resolution*; `target_link_libraries` stays a manual edit in the module's `CMakeLists.txt`. When a cmake module references others, their vcpkg manifests are merged for the build.
  - `--module-ref` treats the dependency name as another module of the project instead of an external one: a cargo path dependency, or (cmake) `add_subdirectory` + an auto-generated `target_link_libraries`. Both modules must already exist and share the same `lang`, and the referenced one must be `kind: "library"`. Paths are resolved from each module's `src`, so modules don't have to sit side by side. Cross-language references (cmake depending on cargo, or vice versa) aren't supported. The referenced library is compiled as part of the module referencing it, so `gram build` doesn't build it on its own a second time (`--module <library>` still does); its artifact still lands next to the consumer's.
  - `--lang gram` creates a nested Gram project: the module's `src` is its own `Gram.ron`, and `--parent <that module>` creates further modules inside it. Its modules are loaded together with the root's (built, referenced and edited the same way), but stay declared in the nested `Gram.ron` with `src` relative to it. Module names are unique across all nested projects, so `--module-ref` reaches a module of a parent project by name. A `gram` module itself takes no dependencies and can't be run; removing it removes its modules too.
  - `gram remove --module <name>` (no dependency given) only removes the module from `Gram.ron` by default; add `--purge` to also delete its directory from disk. Removing from a module that doesn't exist is an error.
- `gram generate [--module <name>]` finds the component schemas of each cargo module (`.ron` files whose first item is `Component(`, outside `target/` and `build*/`) and writes `components.generated.rs` and `Components.generated.hpp` next to its `Cargo.toml`, only when their content changes. `gram build` runs it first, for the whole tree; the generated files are git-ignored. Field types: `Str(N)`, `F32`, `I32`, `U32`, `Bool`.
- `gram run` executes an already-built `kind: "runtime"` module's binary from its `build/<project>/<target>/<config>/` output (`--config`, default `debug`); it doesn't trigger a build first. Omit `--module` to run the project's main module; anything after `--` is forwarded verbatim to the binary. CI doesn't use this; it runs the `gram build --ci` artifact directly.
- `gram ide xcode` (macOS; `vs` on Windows, not tried yet) generates one IDE project for the whole tree in `build/ide/<ide>/`: a CMake superproject (`add_subdirectory` of every cmake module no other one references, a custom target per cargo `dynamic` module running `gram build --module <name> --config <the IDE's configuration>`, the game executable named and placed where `gram build` puts it, next to its library) configured with the IDE's generator. Its configurations are Debug/Release/Final; building the Rust target while the game runs reloads it. `--open` opens it, `--generate-only` only writes the `CMakeLists.txt`.
- `gram deploy` builds and runs the project on a Linux device reachable over SSH (a Steam Deck, a Legion Go S with SteamOS): it `rsync`s the sources there (mirrored, without `.git/`, `target/`, `build/`, `build-*/`, `.vscode/settings.json` and `Devices.ron`), builds the project's own `gram` when it carries one (`gram/Cargo.toml`), runs `gram build --config <config>` (`--module` to build one module), installs Steam's launcher (`~/gram-launch.sh`, plus `~/.gram-launch`: what it runs, the configuration just deployed), then starts the main runtime module on the device's screen with its output streamed back; Ctrl+C or closing the game ends it. With a Steam shortcut (`steam: Some(<app id>)` on the device) the game starts through Steam (`steam://rungameid`), the only way it shows in SteamOS's Game Mode, and in Desktop Mode too; gram follows `~/gram-launch.log`. Without one, or with `--direct`, it starts over ssh (Desktop Mode only), the game's exit code becoming gram's. `--debug` starts the game under `lldb-server` (the device needs `lldb` in its container: `sudo apt install lldb` in the distrobox), stopped before its first instruction, with an `lldb-server platform` next to it, both reached through the run step's ssh tunnels (ports 1234 and 1235), and writes `build/deploy/debug.lldb` (the device as lldb's platform, the executable's path there, the device's source paths mapped onto this tree): once gram says the game waits for the debugger, run VS Code's *Game on device (gram deploy --debug)* (`.vscode/launch.json`), or `lldb -s build/deploy/debug.lldb -o "gdb-remote localhost:1235"`. It stops first in the loader (disassembly, no source): Continue. While stopped at a breakpoint the game draws nothing and takes no input, so the device would think it idle and sleep, cutting the connection: started through Steam, the launcher keeps it awake for the session (`systemd-inhibit --what=idle:sleep`; Steam still dims the screen — touch it or press a button to light it, not power, which suspends). Started over ssh (`--direct`) it can't: only the session on the screen may hold that. C++ and Rust breakpoints both work, `GameLogic`'s reload copies included, its libraries' symbols read from the device through the platform. Setting up the shortcut, once per device: deploy once, then in Desktop Mode *Steam → Games → Add a Non-Steam Game* → `/home/<user>/gram-launch.sh`, start it from the library, and put the `SteamAppId` it logged in `~/gram-launch.log` into `Devices.ron`. The devices are listed in `Devices.ron` next to `Gram.ron`, git-ignored (`Devices(default: Some("legion"), devices: [Device(name, host: <ssh alias or user@address>, dir: <folder under the device's home>, container: Some(<distrobox>), env: Some(<file sourced before building>), steam: Some(<shortcut app id>))])`). `--no-run` only copies and builds; `--dry-run` prints the commands. Setting up a SteamOS device: `docs/gram-native-toolchains.md`, *SteamOS*.
- `gram benchmark` graphs benchmark recordings (the editor's `Engine/Profiler` panel, *Record*: one CSV row per interval in `benchmarks/` next to the game, its settings in a JSON beside it) with `tools/plot_benchmark.py`: one PNG per recording next to it (the passes' GPU ms stacked, the FPS), plus `comparison.png` when there are several (their FPS together, one stacked bar of mean ms per pass each, labelled by the settings that differ). Without paths it takes the `benchmarks/` folder of the game built for `--config` (default `release`, where benchmarks are recorded) and `--target`. It runs the project's `.venv` Python when there is one (`python3 -m venv .venv && .venv/bin/pip install matplotlib`), else `python3`. `--skip` drops warm-up rows (default 1), `--zero` starts the FPS axis at 0, `--show` also opens a window.
  - A Windows device (`os: Windows` in `Devices.ron`; `dir` is then the folder under the user's profile receiving one folder per configuration) is built here instead (`gram build --target windows`, mingw), copied with `tar` through ssh (without `gram.exe` and the static libraries), and started on the desktop through a scheduled task (`gram-launch`, Interactive: a program started over ssh runs in session 0, with no desktop), its output in `<dir>\gram-launch.log`, followed here until it exits. Every script goes as `powershell -EncodedCommand` (base64), so nothing needs quoting. The device needs OpenSSH Server (with a firewall rule for port 22 and PowerShell as its shell; an administrator's key goes in `%ProgramData%\ssh\administrators_authorized_keys`) and a Vulkan driver, no build tools. Windows 11's Smart App Control may block the unsigned `GameLogic.dll` ("An Application Control policy has blocked this file"): turn it off on a development machine (*Windows Security → App & browser control*). `--debug` isn't supported there yet. Debug starts Stopped and its Play button can't build there: Release or Final run the systems from the start.
- `gram init`/`add`/`remove` also keep `.vscode/settings.json` in sync (`rust-analyzer.linkedProjects` for every cargo module), merging with any settings already there.

## Credits

- `Game/Content/textures/autumn_field_puresky_4k.hdr` — [Autumn Field (Pure Sky)](https://polyhaven.com/a/autumn_field_puresky) from Poly Haven, CC0.
- `Game/Content/textures/texture.jpg` — [statue photo](https://pixabay.com/photos/statue-sculpture-figure-1275469/) from Pixabay, the CC0 image used by the Vulkan tutorial's texture chapter.
- `Game/Content/shaders/ffx/` — [AMD FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) headers, © 2021 Advanced Micro Devices, MIT (license text kept in each file).
- `Game/Content/scenes/random-objects/` — my own Blender scene: primitives and Suzanne on a plane, with hand-made materials.

## License

MIT, see [LICENSE](LICENSE). Assets in `Game/Content/third-party/` are not part of the repository and are not covered by it.
