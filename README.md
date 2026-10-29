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
  GramRuntime/        the game's executable: a thin launcher over GramEngine, plus its shaders
docs/                 design notes per wave
```

## Setup

- Rust toolchain (stable, 2024 edition), plus the `x86_64-pc-windows-gnu` target for Windows cross-builds
- CMake and a C++20 compiler; `mingw-w64` for Windows cross-builds (from Linux or macOS)
- vcpkg, with `VCPKG_ROOT` set
- Vulkan SDK (validation layers, `dxc` for the HLSL shaders; MoltenVK on macOS). On Linux, the Vulkan loader comes from the system.

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
gram init <name> [--lang cargo|cmake] [--kind runtime|library] [--target linux,windows,macos] [--git]
gram build [--path <dir>] [--ci] [--target linux|windows|macos]
gram add [<dependency>] [--module <name>] [--lang cargo|cmake|gram] [--parent <gram module>] [--kind runtime|library] [--target linux,windows,macos] [--url <git-url> --tag <tag>] [--vcpkg] [--module-ref]
gram remove [<dependency>] [--module <name>] [--purge]
gram run [--module <name>] [--target linux|windows|macos] [-- <args>]
```

- `gram init` scaffolds `<name>/Gram.ron` plus the initial module (`--lang cargo` delegates to a real Cargo crate; `--lang cmake` scaffolds a multi-file `src/` + `include/<name>/` CMake project). That module also becomes the project's `main`, the default for `gram run`. `--target` sets which platforms the module declares (`linux`/`windows`/`macos`); omit it to default to just the host OS.
- `gram build` compiles every module for the host OS into `build/<project>/<target>/`. `--target` builds a single target instead, and warns (doesn't fail) if that target isn't declared in a module's `targets`. `--ci` builds every declared target the host can build (its own OS, plus `windows` via mingw from anywhere; `linux` ↔ `macos` is skipped with a warning) into `build/<project>_<version>_<target>/`, and can't be combined with `--target`. Every module of the tree, nested ones included, lands in the root project's directory; a project's main runtime module is copied under the project's name (here `Game` and `gram`), any other module under its own. A cmake module's compiled `shaders/` are copied next to its executable.
- `gram add`/`gram remove` create/remove a module (`--module`, upserted if missing) and/or a dependency on it. Adding a dependency a module already has (by name, whatever the source) only prints a warning. `--target` on `gram add` sets the initial target list of a new module, or appends any not-yet-declared targets to an existing one. Dependencies are recorded in `Gram.ron` by origin: `Registry(...)`, `Git(name, url, tag)`, `Vcpkg(...)` or `Module(...)`.
  - `cargo` modules delegate straight to `cargo add`/`cargo rm`, and take plain registry dependencies only.
  - `cmake` modules resolve a dependency either via CMake FetchContent (`--url`/`--tag`, for any git-clonable CMake project) or a vcpkg manifest (`--vcpkg`, for anything with a maintained vcpkg port). Either way, `gram` only wires up dependency *resolution*; `target_link_libraries` stays a manual edit in the module's `CMakeLists.txt`. When a cmake module references others, their vcpkg manifests are merged for the build.
  - `--module-ref` treats the dependency name as another module of the project instead of an external one: a cargo path dependency, or (cmake) `add_subdirectory` + an auto-generated `target_link_libraries`. Both modules must already exist and share the same `lang`, and the referenced one must be `kind: "library"`. Paths are resolved from each module's `src`, so modules don't have to sit side by side. Cross-language references (cmake depending on cargo, or vice versa) aren't supported.
  - `--lang gram` creates a nested Gram project: the module's `src` is its own `Gram.ron`, and `--parent <that module>` creates further modules inside it. Its modules are loaded together with the root's (built, referenced and edited the same way), but stay declared in the nested `Gram.ron` with `src` relative to it. Module names are unique across all nested projects, so `--module-ref` reaches a module of a parent project by name. A `gram` module itself takes no dependencies and can't be run; removing it removes its modules too.
  - `gram remove --module <name>` (no dependency given) only removes the module from `Gram.ron` by default; add `--purge` to also delete its directory from disk. Removing from a module that doesn't exist is an error.
- `gram run` executes an already-built `kind: "runtime"` module's binary from its `build/<project>/<target>/` output; it doesn't trigger a build first. Omit `--module` to run the project's main module; anything after `--` is forwarded verbatim to the binary. CI doesn't use this; it runs the `gram build --ci` artifact directly.
- `gram init`/`add`/`remove` also keep `.vscode/settings.json` in sync (`rust-analyzer.linkedProjects` for every cargo module), merging with any settings already there.
