# Gram Engine

## Overview

Personal game engine project (C++ + Rust), started from rediscovered passion rather than portfolio obligation. Structured in "waves", each producing a working artifact plus a devlog post on `petretiberiu.dev`.

- **Wave 0 — Gram Build** (done): `gram`, a pure-Rust CLI build tool that orchestrates `cargo`, multi-file `cmake` and nested `gram` modules, and builds this whole repo, itself included. Devlog: [Episode 0](https://petretiberiu.dev/posts/gram-engine-00-gram-build/).
- **Wave 1 — Hello Triangle** (in progress): the rendering half is done on Windows, Linux and macOS: a Vulkan 1.3 renderer (dynamic rendering, two frames in flight, swapchain recreation), HLSL shaders compiled to SPIR-V at build time, a docking ImGui overlay, an spdlog-based logger and a layer system. Next is the Rust half: a stateless gameplay `cdylib` the C++ executable loads and hot-reloads.
- **Wave 2 — Ray Tracer**: a Vulkan compute pipeline with spheres, first owned by C++, then driven by Rust entities.

Current task-level progress lives in `.gram.kanban.md`; architecture and design decisions in `CLAUDE.md` and `docs/`. To change Gram Build itself, start with [docs/gram-architecture.md](docs/gram-architecture.md), the developer guide: data model, command flows, backends, how to add a language or a dependency source, tests, and known limits.

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
gram deploy [--device <name>] [--config debug|release|final] [--module <name>] [--no-run] [--dry-run]
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
- `gram deploy` builds and runs the project on a Linux device reachable over SSH (a Steam Deck, a Legion Go S with SteamOS): it `rsync`s the sources there (mirrored, without `.git/`, `target/`, `build/`, `build-*/`, `.vscode/settings.json` and `Devices.ron`), builds the project's own `gram` when it carries one (`gram/Cargo.toml`), runs `gram build --config <config>` (`--module` to build one module), then starts the main runtime module on the device's screen with its output streamed back; Ctrl+C or closing the game ends it, and the game's exit code becomes gram's. The devices are listed in `Devices.ron` next to `Gram.ron`, git-ignored (`Devices(default: Some("legion"), devices: [Device(name, host: <ssh alias or user@address>, dir: <folder under the device's home>, container: Some(<distrobox>), env: Some(<file sourced before building>))])`). `--no-run` only copies and builds; `--dry-run` prints the commands. Setting up a SteamOS device: `docs/gram-native-toolchains.md`, *SteamOS*.
- `gram init`/`add`/`remove` also keep `.vscode/settings.json` in sync (`rust-analyzer.linkedProjects` for every cargo module), merging with any settings already there.
