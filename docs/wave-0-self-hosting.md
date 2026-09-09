# Wave 0 — Self-hosting + `gram add`

Status as of 2026-09-09 (afternoon): `gram build` can now rebuild `gram` itself, and `gram add` manages both new modules and dependencies. Task 0.5 (devlog Episode 0) still open — this doc feeds it.

## Motivation

The council's original Wave 0 take (in the project note) claimed the build tool "knows how to build itself." That wasn't true yet — `gram` was built with plain `cargo build`, never with `gram build`. Closing that gap needed one real capability: `gram build` compiling a real, multi-file, dependency-having Rust crate — not just a single Hello World file.

## New module kind: `lang: "cargo"`

Rather than reimplementing dependency resolution (explicitly a non-goal — "not a package manager"), a `cargo` module delegates to actual Cargo:

```ron
ModuleConfig(
    name: "gram",
    lang: "cargo",
    src: "gram/Cargo.toml",
    deps: [],
    targets: ["linux", "windows"],
)
```

`src` now means "path to this module's `Cargo.toml`" instead of "path to a single source file" — the module can be an arbitrary multi-file crate living in its own subdirectory.

- **`compile_cargo`**: runs `cargo build --manifest-path <src>` (add `--target x86_64-pc-windows-gnu` for the `windows` target), then moves the produced binary from `<manifest_dir>/target/debug/<name>` into the project's standard `builds/` layout. Computed from the manifest's own directory, not the process's cwd — works whether the module sits at the project root or in a subdirectory (needed for Wave 1's `gram-core`/`gram-hot`, each in their own folder).
- **`save_cargo`** (the `gram init`/`gram add` side): runs `cargo init --name <name> --vcs none --edition 2024` in the module's directory. Two flags worth calling out:
  - `--vcs none`, unconditional: git is exclusively a project-level decision (see below) — a module never creates its own nested repo, regardless of whether the overall project is git-tracked.
  - `--edition 2024`, added after a real bug: without it, `cargo init` defaults to **edition 2015**, where `use some_crate::Thing;` doesn't work without an `extern crate some_crate;` first. The resulting compile error (`unresolved import`) reads exactly like a missing/broken dependency, but the dependency was already correctly resolved and linked (`--extern` was present in the `rustc` invocation, confirmed via `cargo build -v`) — it was purely an edition mismatch.

## Self-hosting, verified

`gram-engine`'s own crate now lives in `gram/` (moved out of the repo root), and `gram-build.ron` at the repo root declares it as the one `cargo` module above. Verified end to end, isolated copies:

- `gram build` (native): produces `builds/linux/gram`, runs correctly.
- `gram build --ci`: produces both `builds/gram-engine_0.1.0_linux/gram` and `builds/gram-engine_0.1.0_windows/gram.exe` — the Windows cross-build verified running under `wine`, not just compiling.
- A cargo module in an actual subdirectory (not just the coincidental repo-root case) was tested separately and works — confirms the manifest-relative path fix actually generalizes.

**Bootstrap caveat** (not a flaw): a self-hosting tool still needs one external build to get its first working binary — `cargo build` once, same as any self-hosting compiler (rustc bootstraps from a prior rustc). "Builds itself" means `gram build` *can rebuild* `gram`, not that Cargo was never involved.

**Side effect worth knowing**: the binary move is `fs::rename`, not a copy — so self-building consumes `target/debug/gram` (moved into `builds/`). Running `./target/debug/gram` from a terminal right after a self-build will fail until `cargo build` regenerates it. Not a problem for VS Code's debug launch config (`cargo: { args: ["run", ...] }`), since that triggers its own fresh `cargo build` before attaching — only a plain terminal invocation is affected.

## `gram init --git`: two real bugs fixed

1. `git init` used to run **before** the project directory existed, with no `.current_dir` — it silently git-init'd whatever directory `gram` was invoked *from*, not the new project. Fixed: `project.save(...)` runs first, then (if `--git`) `git init ./<name>/` runs scoped explicitly to the new folder.
2. A related bug during the fix: `save_cargo` used to call `std::env::set_current_dir(&project.path)` — a **global, permanent** process cwd change, not scoped to just that one `cargo init` subprocess. It broke the reordered `git init ./<name>/` (which then resolved relative to the wrong, silently-shifted cwd, producing a doubled `hello-world/hello-world/` path). Fixed by computing `manifest_dir` via `project.path.join(...)` instead — no global cwd mutation, matching the pattern `compile_cargo` already used correctly.

Verified: `--git` now produces exactly one `.git`, at the project root, nowhere else; no `--git` produces zero `.git` folders anywhere (cargo modules never leave a stray nested repo either way).

## `.vscode/settings.json`, auto-generated

Opening a `gram-build.ron` project folder directly in VS Code confuses rust-analyzer if there's no `Cargo.toml` at the folder root (the actual crate can live in a subdirectory, e.g. `main/`). Fix: `gram init` (and `gram add`, when it creates a new module) writes `.vscode/settings.json` with `rust-analyzer.linkedProjects` listing every `cargo` module's manifest:

```json
{
    "rust-analyzer.linkedProjects": [
        "main/Cargo.toml"
    ]
}
```

Skipped entirely if the project has no cargo modules. `gram-engine`'s own `.vscode/settings.json` was hand-created the same way, pointing at `gram/Cargo.toml`.

## `gram add <dependency> [--module <name>]`

One command instead of a separate "create module" command — `--module` is create-if-missing (an upsert), `<dependency>` is optional:

- `gram add --module gram-hot` (no dependency) → creates `gram-hot` as an empty cargo module if it doesn't exist yet; no-op if it already does.
- `gram add serde --module gram-core` → ensures `gram-core` exists (creating it first if needed), then runs real `cargo add serde --manifest-path gram-core/Cargo.toml` on it.

Ordering matters for durability: module creation is written to `gram-build.ron` (and `.vscode/settings.json`) **immediately**, before attempting the dependency step — so if `cargo add` later fails (bad crate name, network), the newly-created module directory stays correctly tracked instead of becoming an orphan. The dependency name is only appended to the module's `deps` list *after* `cargo add`'s exit status confirms success (an earlier version recorded it optimistically beforehand — fixed).

**Open question, not yet resolved**: `ModuleConfig.deps` was originally scoped as *inter-module* dependencies (e.g. `gram-hot` depending on `gram-core`, relevant from Wave 1). `gram add`'s `deps` bookkeeping now records *external crate* dependencies instead (`anyhow`, `serde`, ...) — which Cargo's own `Cargo.toml` already tracks natively. These are two different concerns currently sharing one field; worth deciding before Wave 1 needs `deps` for its original inter-module purpose.

## What's next

- Task 0.5 — devlog Episode 0, still open.
- Decide `deps`' actual scope (inter-module vs. external-crate) before Wave 1.
- Wave 1 — `gram-core`/`gram-hot` as separate `cargo` modules exercises the subdirectory + inter-module-`deps` path for real.
