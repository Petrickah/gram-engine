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

The `gram` crate lives at `gram/Cargo.toml`, not the repo root, so plain `cargo` needs `--manifest-path` (or `cd gram` first):

- Build: `cargo build --manifest-path gram/Cargo.toml`
- Run: `cargo run --manifest-path gram/Cargo.toml -- <args>` (e.g. `-- --help`, `-- init hello-world`)
- Test: `cargo test --manifest-path gram/Cargo.toml`
- Self-hosted (once built once): from the repo root, `gram/target/debug/gram build`/`build --ci` builds `gram` using the checked-in root `Gram.ron` — this is what CI would actually run.

## Architecture (current)

Rust binary crate `gram` (package name in `gram/Cargo.toml`, edition 2024), built with `clap` (derive API) + `serde`/`ron-rs`. Split across `gram/src/`:

- `main.rs` — `Cli`/`Commands`: `init`, `build`, `add`, `remove`, `run` (see `README.md` for the full flag reference).
- `common.rs` — the actual engine. `BuildConfig` is the `Gram.ron` project (`name`, `version`, `main` — the default runtime module, set once by `gram init` — and `modules: Vec<ModuleConfig>`). `ModuleConfig` is one module (`name`, `lang`: `"cargo"`|`"cmake"`, `kind`: `"runtime"`|`"library"` — cmake-only — `src`, `deps`, `targets`: `"linux"`|`"windows"`, `macos` planned for Wave 2/MoltenVK).
- `init.rs`/`build.rs`/`add.rs`/`rm.rs`/`run.rs` — thin per-subcommand wrappers around `BuildConfig` methods.

Module language behavior:
- `cargo` modules delegate straight to real `cargo` (`init`/`add`/`rm`/`build`), including cross-compiling to `x86_64-pc-windows-gnu` via the vendored `.cargo/config.toml` (mingw linker, static CRT).
- `cmake` modules are scaffolded by `gram` (`CMakeLists.txt` + `src/main.cpp`, or for a library `include/<name>/<name>.hpp` + `src/<name>.cpp`) and built by invoking `cmake` directly. A managed region in `CMakeLists.txt` (`# --- gram dependencies ---` markers) lets `add`/`remove` edit `FetchContent_Declare` blocks (`--url`/`--tag`) or vcpkg `find_package` calls (`--vcpkg`, needs `VCPKG_ROOT`; the generated `find_package`/`target_link_libraries` are a heuristic guess and sometimes need a manual fix — e.g. vcpkg's `vulkan` port resolves via CMake's built-in `FindVulkan` module, not a vcpkg-provided config file) as text, without a real CMake parser. Windows cross-compile always sets `CMAKE_SYSTEM_NAME=Windows` + the mingw compilers directly, statically linking the mingw runtime (`-static-libgcc -static-libstdc++ -static`, mirroring `+crt-static` on the cargo side) so the `.exe` runs standalone under `wine`/on real Windows; a vcpkg module additionally gets `-DVCPKG_TARGET_TRIPLET=x64-mingw-static` so vcpkg cross-builds its dependencies for mingw instead of defaulting to the MSVC-based `x64-windows` triplet (which vcpkg picks by default for any Windows target, ignoring the manually-set compilers).
- `gram` modules (`lang: "gram"`, `src: "<dir>/Gram.ron"`) are nested Gram projects. `BuildConfig::open` loads their modules into the same flat list with `src` prefixed by the nested directory (and a `#[serde(skip)] project` field remembering which file they belong to); `BuildConfig::write` splits them back into each `Gram.ron`. Module names are unique across the whole tree. `gram add --parent <gram module>` creates a module inside one. The repo itself uses this: root `Gram.ron` holds `gram` (→ `gram/Gram.ron`: `gram-cli`, `gram-common`), `GramEngine`, and `Game` (→ `Game/Gram.ron`: `GramRuntime`, referencing `GramEngine` in the root).
- Either language supports `--module-ref` on `gram add`: one module depending on another already in the project (must share the same `lang`; the referenced module must be `kind: "library"`) — a cargo path dependency, or a cmake `add_subdirectory` + `target_link_libraries` pair. Cross-language references aren't supported.

Builds land in `build/<project>/<target>/` (plain `gram build`) or `build/<project>_<version>_<target>/` (`--ci`, one per `targets` entry). Every module of the tree (nested ones included) lands in the root project's directory. A project's main runtime module is copied under the project's name (`Game`, `gram`), any other module under its own. `gram run` executes an already-built `runtime` module's binary directly — it doesn't trigger a build. `gram init`/`add`/`remove` also (re)generate `.vscode/settings.json` (`rust-analyzer.linkedProjects`) for every cargo module, so rust-analyzer doesn't get confused by a project root that isn't itself a Cargo project.

No C++/Rust interop yet. In Wave 1 it needs no build-system bridge: the C++ executable only `dlopen`s/`LoadLibrary`s the Rust `cdylib`, so `gram` needs `crate-type = ["cdylib"]` support, copying the library next to the executable, and later the `.ron` schema → Rust/C++ code generation step.

## Non-goals (current)

- No multiplayer/networking, no visual editor, no emulators/plugins — noted ideas, not now.
- No performance/optimization focus — functional correctness first.
- **Cross-compilation is scoped to `--ci` builds only** (added 2026-09-09): a `gram build --ci` iterates every target in `ModuleConfig.targets` (currently `linux`/`windows`, via `x86_64-w64-mingw32-g++` and `rustc --target x86_64-pc-windows-gnu`); a plain `gram build` only compiles for the host it's run on, no flag needed to pick that. Enabled now because Wave 0 has no native deps to cross-compile (no Vulkan yet) — expect to revisit once Wave 1 introduces platform-specific dependencies that won't cross-compile as cleanly.
- **No CI/CD until an explicit "Personal Builds" stage.** Planned design when that starts: GitHub Actions (free hosted runners) run classic Rust CI (`cargo build`/`test`/`clippy`) on every push; only on success, one step uses `tailscale/github-action` (ephemeral, ACL-scoped tailnet node, no exposure) to reach the private network and either mirror to Gitea (triggers the existing Jenkins webhook) or call Jenkins directly. A permanent self-hosted GitHub Actions runner was considered and rejected — it works (outbound-poll only, no port to open) but needs an ongoing-maintained process; the hosted-runner + ephemeral-Tailscale-hop design needs nothing running when idle.

## Repo model

Private GitHub repo (`Petrickah/gram-engine`) is the sole origin for now — deliberately not the Gitea-first convention used elsewhere in the author's `Projects` monorepo, chosen for durable code history and eventual Patreon early-access (repo-level separation when that happens, not a branch split). Gitea gets added later only as an automatic pull-mirror of GitHub, once "Personal Builds" actually starts — not before.
