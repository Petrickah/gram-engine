# Gram Engine

## Overview

Personal game engine project (Rust + C++), started from rediscovered passion rather than portfolio obligation. Structured in "waves" — each wave produces a working artifact plus a devlog post. This repo starts with **Gram Build**, a pure-Rust CLI build tool (`gram init`/`build`/`add`/`remove`/`run`) that orchestrates `cargo` and multi-file `cmake` modules, which later grows to compile the engine's own C++/Rust modules.

Full design notes, wave breakdown, and architecture decisions live in the author's private vault (`05_Projects/notes/Gram Engine.md`) — not included in this repo.

## Setup

Requires a Rust toolchain (stable, 2024 edition).

```
cargo build
```

## Usage

```
gram init <name> [--lang cargo|cmake] [--kind runtime|library] [--target linux,windows,macos]
gram build [--path <dir>] [--ci] [--target linux|windows|macos]
gram add [<dependency>] [--module <name>] [--lang cargo|cmake|gram] [--parent <gram module>] [--kind runtime|library] [--target linux,windows,macos] [--url <git-url> --tag <tag>] [--vcpkg] [--module-ref]
gram remove [<dependency>] [--module <name>] [--purge]
gram run [--module <name>] [--target linux|windows|macos] [-- <args>]
```

- `gram init` scaffolds `<name>/gram-build.ron` plus the initial module (`--lang cargo` delegates to a real Cargo crate; `--lang cmake` scaffolds a multi-file `src/` + `include/<name>/` CMake project). That module also becomes the project's default for `gram run`. `--target` sets which platforms the module declares support for (`linux`/`windows`/`macos`) — omit it to default to just the host OS.
- `gram build` compiles every module for the host OS by default; `--target` cross-builds a single target instead (local layout, `builds/<target>/`), and warns (doesn't fail) if that target isn't declared in the module's `targets`. `--ci` cross-compiles every target listed in each module's `targets` that the host can build (its own OS, plus `windows` via mingw from anywhere — `linux`↔`macos` is skipped with a warning) into `../builds/<project>_<version>_<target>/` — can't be combined with `--target`, since `--ci` already builds every declared target.
- `gram add`/`gram remove` create/remove a module (`--module`, upserted if missing) and/or a dependency on it. `--target` on `gram add` sets the initial target list when creating a module, or appends any not-yet-declared targets to an existing one:
  - `cargo` modules delegate straight to `cargo add`/`cargo rm`.
  - `cmake` modules resolve a dependency either via CMake FetchContent (`--url`/`--tag`, for anything that's a plain git-clonable CMake project) or a vcpkg manifest (`--vcpkg`, for anything with a maintained vcpkg port — needs `VCPKG_ROOT` set). Either way, `gram` only wires up dependency *resolution*; `target_link_libraries` stays a manual edit in the module's `CMakeLists.txt`.
  - `--module-ref` treats the dependency name as another module already in this project instead of an external one — a cargo path dependency, or (cmake) `add_subdirectory` + an auto-generated `target_link_libraries` (gram knows the exact target name here, unlike for FetchContent/vcpkg). Both modules must already exist, share the same `lang`, and the referenced one must be `kind: "library"`. Cross-language references (cmake depending on cargo, or vice versa) aren't supported yet.
  - `--lang gram` creates a nested Gram project: the module's `src` is its own `Gram.ron`, and `--parent <that module>` creates further modules inside it. Its modules are loaded together with the root's (built, referenced and edited the same way, artifacts in the root's `build/`), but stay declared in the nested `Gram.ron` with `src` relative to it. Module names are unique across all nested projects, so `--module-ref` reaches a module of a parent project by name. A `gram` module itself takes no dependencies and can't be run; removing it removes its modules from the config too.
  - `gram remove --module <name>` (no dependency given) only removes the module from `gram-build.ron` by default; add `--purge` to also delete the module's directory from disk.
- `gram run` executes an already-built `kind: "runtime"` module's binary straight from its `builds/<target>/` output — it doesn't trigger a build first. Omit `--module` to run the project's default module; anything after `--` is forwarded verbatim to the binary. CI doesn't use this — it runs the `gram build --ci` artifact directly.
