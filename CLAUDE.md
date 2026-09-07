# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

- Build: `cargo build`
- Run: `cargo run -- <args>` (e.g. `cargo run -- --help`, `cargo run -- init hello-world`)
- Test: `cargo test`

## Architecture

Single Rust binary crate, `gram` (package name in `Cargo.toml`), built with `clap` (derive API) for subcommand parsing. Currently one file, `src/main.rs`, defining `Cli`/`Commands` (`init`, `build`).

`init` and `build` are stubs (`todo!()`) — the real logic (parsing a `gram-build.ron` project definition via `serde` + `ron-rs`, invoking a compiler) is scoped as separate follow-up work, written by the project owner rather than delegated.

No CI/CD wired yet (deliberate — see the project's own wave notes). No C++ interop yet — that's introduced in a later wave, at which point CMake + Corrosion get added as a build step invoked by `gram build`.
