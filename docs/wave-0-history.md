# Gram Build — the story so far

A single, chronological account of Wave 0, start to present: what existed at each step, why it changed, what broke and got fixed. The three other `docs/wave-0-*.md` files cover individual additions in full technical depth (exact commands, exact bugs, exact verification) — this one exists so the *sequence* stays straight, since the tool's shape today is quite different from its shape a week ago, and code examples from different points in that history shouldn't get mixed.

**Where things stand right now**, if you only need that: skip to "Where it stands today."

## Before Wave 0: why a build tool first

Gram Engine is a personal game engine (Rust + C++), started from rediscovered passion, not a portfolio obligation — no deadline, structured in "waves," each wave producing one working artifact plus a devlog post. Before writing a renderer, an ECS, or opening a window, the project needed to know how it was going to build itself: which compiler, how C++ and Rust link together, where a binary ends up when built for more than one platform. Gram Build — the `gram` CLI — is that answer, and Wave 0's whole scope.

The name is provisional: Gram, the sword of Sigurd, following the same start-descriptive-then-earn-a-real-name philosophy as Scimitar → Anvil internally at Ubisoft.

## Task 0.1 — Scaffold (2026-09-06ish)

A Rust binary, `clap` derive, two subcommands — `init` and `build` — both `todo!()` stubs. Nothing worked yet; the point was a CLI skeleton that parsed correctly. Private GitHub repo (`Petrickah/gram-engine`) created as sole origin — deliberately not the Gitea-first convention used elsewhere in the author's monorepo, for durable code history and eventual Patreon early access.

## Task 0.2 — `gram init` (single-file, cpp/rs)

`gram init <name> --lang cpp|rs` created `<name>/gram-build.ron` (a `BuildConfig` with one `ModuleConfig` named `main`) plus a matching Hello World source file — `src/main.cpp` or `src/main.rs`. The module's `targets` list was auto-set to `[<host OS>]` (`std::env::consts::OS`), no input needed.

The project definition format: **RON** (Rusty Object Notation) — Serde-native, human-readable, and reusable later for asset definitions rather than inventing a second format. Struct names are serialized explicitly (`PrettyConfig::struct_names(true)`) so a `gram-build.ron` is self-describing on open:

```ron
BuildConfig(
    name: "hello-world",
    version: "0.1.0",
    modules: [
        ModuleConfig(
            name: "main",
            lang: "cpp",
            src: "src/main.cpp",
            deps: [],
            targets: ["linux"],
        ),
    ],
)
```

`deps` existed in the schema from the start but was unused — anticipated for Wave 1, once modules actually depend on each other. (It's genuinely used now — see "Module references" below — though not quite the way it was first imagined; more on that later.)

## Task 0.3 — `gram build` compiles

Reads `gram-build.ron`, extracts `lang`/`src`, invokes the right compiler, places the binary in `builds/<target>/<bin>`. `cpp` compiled via `g++`; `rs` via `rustc`. Verified by actually running the output, not just checking it compiled.

## Task 0.4 — `gram build --ci`

Iterates every target in a module's `targets` (`linux`/`windows`, via `x86_64-w64-mingw32-g++` and `rustc --target x86_64-pc-windows-gnu`) and places each binary in `../builds/<project>_<version>_<target>/<binary>` — a sibling of the project directory, not inside it, so CI artifacts and local artifacts never mix. Windows cross-builds were verified running under `wine`, not just compiling.

**Decisions made here, revisited later:**
- Cross-compilation scoped to `--ci` only — a plain `gram build` always stays on the host system. Fine while Wave 0 had no native dependencies (no Vulkan yet).
- The `--ci` layout was simplified from an original plan of `builds/<project>/<date>-<commit>/<platform>/<binary>` — the date/commit granularity was dropped since no real CI pipeline existed yet to consume it (YAGNI, applied mid-stream, not upfront).
- `--path` does a `chdir`, not path-prefixing — every filesystem-touching function works relative to "cwd = project directory," which is simpler than threading a path argument through all of them. (This decision caused a real bug later — see Task 0.6.)

At this point `gram` could build a single-file Hello World, in either C++ or Rust, for one or every declared target. Task 0.5 (a devlog article for this stage) was queued but never finished before the project moved on — it's still open today, and the confusion in that draft is part of why this document exists.

## Task 0.6 — Self-hosting + `gram add` (2026-09-09)

The project note had already claimed Wave 0 was "a build tool that knows how to build itself" — which wasn't true yet. `gram` was still compiled with plain `cargo build`, never `gram build`. Making the claim real needed `gram build` to handle an actual multi-file Rust crate with dependencies, not a single Hello World file.

**New module kind: `lang: "cargo"`.** Rather than reimplementing dependency resolution — explicitly a non-goal, "not a package manager" — a `cargo` module delegates to real Cargo. `src` now means "path to this module's `Cargo.toml`," not "path to a source file," letting a module be an arbitrary multi-file crate in its own subdirectory. `compile_cargo` ran `cargo build --manifest-path <src>` and moved the resulting binary into the standard `builds/` layout; `save_cargo` (the `init`/`add` side) ran `cargo init --name <name> --vcs none --edition 2024`.

`gram`'s own source moved into `gram/`, and the repo-root `gram-build.ron` declared it as that one `cargo` module. `gram build` (native) and `gram build --ci` (cross-compiling to Windows, verified under Wine again) both rebuilt `gram` successfully. The tool that builds everything had rebuilt itself — with the expected caveat that a self-hosting tool still needs one external `cargo build` to produce its first binary, same as any self-hosting compiler bootstrapping from a prior version of itself.

**`gram add <dependency> [--module <name>]`** arrived alongside this: one command instead of two, `--module` create-if-missing, the dependency name optional (omit it to just create an empty module). Module creation is persisted to `gram-build.ron` immediately, before the dependency step runs — so a failed `cargo add` (bad name, no network) never leaves an untracked, orphaned module directory on disk.

**Three real bugs, found and fixed:**
1. `gram init --git` ran `git init` *before* the project directory existed and without an explicit target — it silently initialized whatever directory `gram` happened to be invoked from. Fixed by running `git init` after project creation, scoped to the new path explicitly.
2. `cargo init` without `--edition` defaulted to **edition 2015**, where `use some_crate::Thing;` doesn't compile without `extern crate` first. The resulting error read exactly like a missing/broken dependency — a genuine misdiagnosis trap — when the dependency was already correctly resolved. Fixed by passing `--edition 2024` explicitly.
3. Fixing bug 2 surfaced a second one: a helper used `std::env::set_current_dir` to point at the module directory — a **global, permanent** change to the process's cwd, not scoped to just that `cargo init` subprocess. It broke `git init`'s now-corrected path resolution (itself relative to the now-silently-shifted cwd), producing a doubled `hello-world/hello-world/` directory. Fixed by computing paths via `project.path.join(...)` directly instead of mutating global state — the same pattern `compile_cargo` was already using correctly.

`.vscode/settings.json` was also added, auto-generated with `rust-analyzer.linkedProjects` listing every `cargo` module's manifest — otherwise rust-analyzer gets confused when the project root itself isn't a Cargo project (the actual crate can live in a subdirectory).

**Open question raised here, not yet resolved:** `ModuleConfig.deps` was designed for *inter-module* dependencies (`gram-hot` depending on `gram-core`, anticipated for Wave 1), but `gram add` started using it to record *external crate* dependencies (`anyhow`, `serde`) instead — two different concepts sharing one field, undecided which way to split. It comes back up below.

Full detail: `docs/wave-0-self-hosting.md`.

## Task 0.7 — cmake modules, dependency management, `gram run`, module references (2026-09-16)

The biggest single jump, done in one extended session, in roughly this order:

**`gram remove` arrives, with a bug.** A `remove` counterpart to `add` was started; an early version of `rm_dependency` deleted the *entire module* every time it ran, regardless of whether a dependency name was given — contradicting its own stated behavior. Fixed with an early-return once a dependency name is present, so removing a dependency and removing a whole module stay genuinely separate operations.

**`lang: "cpp"`/`"rs"` (single-file) removed, `lang: "cmake"` added.** Wave 1 needs real, multi-file C++ modules with external dependencies — Vulkan eventually, tested concretely first with olcPixelGameEngine. The old single-file mode couldn't support that, and rather than keep two divergent C++ code paths (one with dependency support, one without) indefinitely, the old one was dropped outright. Every module also gained a `kind`: `"runtime"` (executable) or `"library"` (dynamic — `add_library(... SHARED)` for cmake). A `cmake` module is multi-file from creation: `src/`, plus `include/<name>/` for a library's public header.

**Two ways to resolve an external cmake dependency**, both through `gram add`/`gram remove`:
- **FetchContent** (default, `--url`/`--tag`) — for anything that's a plain git-clonable CMake project.
- **vcpkg manifest mode** (`--vcpkg`) — for anything with a maintained port, where the build itself is complex enough (system deps, cross-platform patches) that reusing someone else's solved version is worth a second dependency mechanism. Needs `VCPKG_ROOT` set.

Both only wire dependency *resolution* into a managed, marker-delimited region of `CMakeLists.txt` — `target_link_libraries` stays a manual edit, since gram can't reliably guess the right CMake target name for an arbitrary external package. `gram remove` doesn't need to know which mechanism was used; it just cleans up whatever's there.

Two more bugs surfaced testing this for real: a stale CMake build directory silently ignored a newly-added `VCPKG_ROOT` toolchain file (`CMAKE_TOOLCHAIN_FILE` only takes effect on a build tree's *first* configure) — fixed by always passing `--fresh`. And a vcpkg build of `fmt` failed with a misleading "compiler not set"-looking error that was actually just a missing system `pkg-config`, a reminder to check vcpkg's own buried logs before trusting the surface error.

**`gram run`** followed: execute an already-built `kind: "runtime"` module's binary straight from `builds/<target>/`, without triggering a build. `BuildConfig` gained a `main` field (set by `gram init`) so `--module` can be omitted; arguments after `--` are forwarded verbatim to the binary regardless of their position relative to gram's own flags — needed for running something that parses its own subcommands (`gram run --module gram -- build`). Deliberately no `--ci` on `run` — it's for the person writing code, not for a pipeline, which runs the `--ci` artifact directly.

**`gram remove --purge`** — removing a module stayed config-only by default (unchanged behavior); `--purge` additionally deletes the module's directory from disk. Opt-in, not opt-out, on purpose: an ordinary `remove` shouldn't ever be destructive to files by accident.

**`--target` on `init`/`add`/`build`.** `targets` had been fixed automatically to the host OS with no way to change it short of hand-editing the file. Now `gram init`/`gram add --module <new>` accept `--target linux,windows`; `gram add --module <existing> --target <t>` appends idempotently. `gram build --target <t>` cross-builds a single target locally, rejected if combined with `--ci` (which already covers every declared target). Along the way, a real correctness gap got fixed: `gram build` used to silently build for the host OS even when a module didn't declare that OS as supported at all — it now warns (doesn't fail) when the resolved target isn't in a module's declared list.

**`--module-ref` — cross-module dependencies within the same project, same language only.** `gram add <module> --module <consumer> --module-ref` treats `<module>` as another module already in `gram-build.ron`, not an external package:
- cargo → cargo: `cargo add <dep> --path <dep> --manifest-path ...` — a bare relative path works because every gram module is always a direct sibling of the project root.
- cmake → cmake: `add_subdirectory(...)` plus an auto-generated `target_link_libraries(...)` — here, unlike FetchContent/vcpkg, gram *does* know the exact CMake target name (it generated it), so it wires the link itself instead of leaving that step manual.

Validated up front: both modules must exist, can't be the same module, the referenced one must be `kind: "library"`, and both must share the same `lang` — cross-language references (a cmake module depending on a cargo module, or vice versa) are explicitly rejected, not attempted. That specific case — a C++ module statically linking a Rust module — is exactly Wave 1's `gram-core`/`gram-hot` architecture, and deliberately wasn't generalized into this feature: it means bridging two build systems with FFI codegen (`cxx` + Corrosion) in between, a different kind of problem from linking two same-language targets, and one that deserves solving concretely when Wave 1 starts rather than abstractly before a second real use case exists to validate the shape.

Building this surfaced two more bugs, both invisible until a `"library"`-kind cargo module was actually exercised: `save_cargo` had been ignoring `kind` entirely, so a "library" cargo module still got a plain binary-crate scaffold with no library target to reference (fixed: pass `--lib` when `kind == "library"`); and `compile_cargo` unconditionally expected a binary to move into `builds/`, which crashed with "No such file or directory" on any library-kind module (fixed: a library module just compiles, no move step).

Verified end to end with real compilation and linking, not just inspecting the generated files — a cargo library exporting a function, called by a runtime module through the path dependency; a cmake shared library, called through the auto-wired link; every validation guard producing its intended error.

This also partially answers Task 0.6's open question about `ModuleConfig.deps`: a module-ref dependency's name is pushed into `deps` exactly like an external one's, so both concepts now genuinely coexist in the same list — still not structurally distinguished (telling them apart means cross-checking against `self.modules`' names), which hasn't caused a problem yet but is worth remembering if it needs to.

Full detail: `docs/wave-0-cmake-modules.md`.

## Where it stands today

```
gram init <name> [--lang cargo|cmake] [--kind runtime|library] [--target linux,windows]
gram build [--path <dir>] [--ci] [--target linux|windows]
gram add [<dependency>] [--module <name>] [--lang cargo|cmake] [--kind runtime|library] [--target linux,windows] [--url <git-url> --tag <tag>] [--vcpkg] [--module-ref]
gram remove [<dependency>] [--module <name>] [--purge]
gram run [--module <name>] [--target linux|windows] [-- <args>]
```

A current `gram-build.ron` (two modules: a cargo runtime and a cmake library it references) looks like:

```ron
BuildConfig(
    name: "demo",
    version: "0.1.0",
    main: "main",
    modules: [
        ModuleConfig(
            name: "main",
            lang: "cargo",
            kind: "runtime",
            src: "main/Cargo.toml",
            deps: ["core"],
            targets: ["linux", "windows"],
        ),
        ModuleConfig(
            name: "core",
            lang: "cargo",
            kind: "library",
            src: "core/Cargo.toml",
            deps: [],
            targets: ["linux"],
        ),
    ],
)
```

Notably **not** present anywhere above: `lang: "cpp"`/`"rs"` (removed, replaced by `cmake`) — a code sample using them belongs to Task 0.2–0.4's story, not to the tool as it exists now. If something written about Gram Build cites a command or a `.ron` shape, cross-check it against this file's timeline before trusting it — that mismatch (old commands, new commands, same paragraph) is exactly what prompted writing this document.

## What's next

- Task 0.5 (the Episode 0 devlog) is still the only genuinely unfinished item from Wave 0's original scope — and now has an accurate history to draw from instead of the stale draft that mixed pre- and post-Task-0.7 material.
- Wave 1 (Hello Triangle): C++ owning `main()` and the renderer core, `gram-core` linked statically via `cxx` + Corrosion, `gram-hot` reloaded at runtime as a `cdylib` — the cmake↔cargo module reference this document's Task 0.7 section deliberately left undone.
- `ModuleConfig.deps`'s dual meaning (inter-module vs. external) still isn't structurally resolved — worth deciding before it actually causes confusion rather than after.
