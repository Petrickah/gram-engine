# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. It is self-contained: this repo is meant to be cloned standalone on dev machines (Windows, macOS) that don't have the author's Obsidian vault or the `Projects` monorepo it's normally a submodule of — don't assume either is available. Current task-level progress lives in `.gram.kanban.md`; check it first.

## What this is

Gram — a personal game engine (Rust + C++), started from rediscovered passion, not a portfolio obligation. No deadline. Structured in "waves": each wave produces one working artifact plus a devlog post (`petretiberiu.dev`).

## Waves

- **Wave 0 — Gram Build**: a pure-Rust CLI build tool (`gram init`, `gram build`) — no C++, no runtime deps. Defines projects via a `.ron` file (serde + `ron-rs`), a format reused later for asset definitions.
- **Wave 1 — Hello Triangle**: window + Vulkan + triangle, C++/Rust interop. C++ owns `main()` and the renderer core (static, never reloaded). Rust is the reloadable gameplay layer, split into two crates for `TypeId` stability across recompiles: `gram-core` (linked statically into the C++ binary via `cxx` + Corrosion, defines the `bevy_ecs` `World` and Component/Resource types) and `gram-hot` (`cdylib`, reloaded at runtime via `LoadLibrary`/`dlopen`, contains the Systems). The dynamic reload boundary is plain `extern "C"` / `#[repr(C)]`, not `cxx` — `cxx` is only used between `gram-core` (static) and C++. Build system: CMake + Corrosion (Premake was considered and rejected — it doesn't generate a CMake project on Linux).
- **Wave 2 — Ray Tracer**: Vulkan compute shader pipeline, spheres driven by Rust entities (position/color/radius).
- **Wave 3+**: raster pipeline, post-processing, asset manager, world serialization — noted, unplanned.

## Methodology — who writes what

- **User writes**: anything that's a new skill being learned for the first time (a CLI with subcommands the first time, Vulkan code following the official tutorial, ECS entity/component/system definitions) — the parts with real learning value.
- **Claude Code writes**: scaffolding (CLI boilerplate, build system wiring, Rust↔C++ interop glue, CMake+Corrosion setup) — plumbing without learning value, once a pattern is already familiar.
- Rule of thumb: first time doing something new → user writes it. Something done many times before (Cargo.toml config, adding a dependency) → delegate to Claude Code.

## Commands

- Build: `cargo build`
- Run: `cargo run -- <args>` (e.g. `cargo run -- --help`, `cargo run -- init hello-world`)
- Test: `cargo test`

## Architecture (current)

Single Rust binary crate, `gram` (package name in `Cargo.toml`), built with `clap` (derive API). Currently one file, `src/main.rs`, defining `Cli`/`Commands` (`init`, `build`) — both stubs (`todo!()`) pending the tasks that parse `gram-build.ron` (via `serde` + `ron-rs`) and invoke a compiler.

No C++ interop yet — introduced in Wave 1, where CMake + Corrosion get invoked as a build step from `gram build` for modules declaring a C++/Rust mix.

## Non-goals (current)

- No multiplayer/networking, no visual editor, no emulators/plugins — noted ideas, not now.
- No performance/optimization focus — functional correctness first.
- **Cross-compilation is scoped to `--ci` builds only** (added 2026-09-09): a `gram build --ci` iterates every target in `ModuleConfig.targets` (currently `linux`/`windows`, via `x86_64-w64-mingw32-g++` and `rustc --target x86_64-pc-windows-gnu`); a plain `gram build` only compiles for the host it's run on, no flag needed to pick that. Enabled now because Wave 0 has no native deps to cross-compile (no Vulkan yet) — expect to revisit once Wave 1 introduces platform-specific dependencies that won't cross-compile as cleanly.
- **No CI/CD until an explicit "Personal Builds" stage.** Planned design when that starts: GitHub Actions (free hosted runners) run classic Rust CI (`cargo build`/`test`/`clippy`) on every push; only on success, one step uses `tailscale/github-action` (ephemeral, ACL-scoped tailnet node, no exposure) to reach the private network and either mirror to Gitea (triggers the existing Jenkins webhook) or call Jenkins directly. A permanent self-hosted GitHub Actions runner was considered and rejected — it works (outbound-poll only, no port to open) but needs an ongoing-maintained process; the hosted-runner + ephemeral-Tailscale-hop design needs nothing running when idle.

## Repo model

Private GitHub repo (`Petrickah/gram-engine`) is the sole origin for now — deliberately not the Gitea-first convention used elsewhere in the author's `Projects` monorepo, chosen for durable code history and eventual Patreon early-access (repo-level separation when that happens, not a branch split). Gitea gets added later only as an automatic pull-mirror of GitHub, once "Personal Builds" actually starts — not before.
