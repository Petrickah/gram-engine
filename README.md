# Gram Engine

## Overview

Personal game engine project (Rust + C++), started from rediscovered passion rather than portfolio obligation. Structured in "waves" — each wave produces a working artifact plus a devlog post. This repo starts with **Gram Build**, a pure-Rust CLI build tool (`gram init` / `gram build`), which later grows to compile the engine's own C++/Rust modules.

Full design notes, wave breakdown, and architecture decisions live in the author's private vault (`05_Projects/notes/Gram Engine.md`) — not included in this repo.

## Setup

Requires a Rust toolchain (stable, 2024 edition).

```
cargo build
```

## Usage

```
gram --help
gram init <name>      # scaffold a new Gram project (gram-build.ron + Hello World source)
gram build             # compile the current project
gram build --ci        # same, but CI-standard artifact layout
```

`init` and `build` are currently stubs — see the CLI's own `todo!()` messages for what's pending.
