# Wave 0 — cmake modules, dependency management, `gram run`, module references

Status as of 2026-09-16: `gram` now orchestrates real multi-file C++ (cmake) modules alongside `cargo` ones, resolves external dependencies two ways (FetchContent, vcpkg), runs already-built binaries, and can wire two modules in the same project together as a dependency — same-language only for now. This closes most of the gap Wave 1 needs from the build tool; the remaining piece (a cmake module statically linking a cargo module through Corrosion + `cxx`) is deliberately out of scope here, see "What's next".

## Motivation

Wave 1 needs real C++ modules — multi-file, with external dependencies (Vulkan SDK eventually, tested concretely with olcPixelGameEngine and `fmt`) — which the old single-file `cpp`/`rs` module kind couldn't support at all. Rather than wait for Wave 1 to start, this was pulled forward: it's scaffolding work (CMake setup, dependency wiring), not learning-value work, so it fit the project's own "Claude Code writes plumbing" rule even mid-Wave-0.

## New module kind: `lang: "cmake"`, plus `kind`

Every module (`cargo` or `cmake`) now has a `kind`: `"runtime"` (executable) or `"library"` (dynamic — `add_library(... SHARED)` for cmake; `cargo init --lib` for cargo, see the cargo-library bug below). `#[serde(default)]` keeps old `gram-build.ron` files without the field parseable (defaults to `"runtime"`).

A `cmake` module is multi-file from creation — `src/` plus, for a library, `include/<name>/` for its public header:

```
main/
├── CMakeLists.txt
└── src/
    └── main.cpp

engine/                      # kind: "library"
├── CMakeLists.txt
├── include/engine/engine.hpp
└── src/engine.cpp
```

The old single-file `cpp`/`rs` module kind (direct `g++`/`rustc` invocation, no CMake) is **removed entirely**, not deprecated alongside `cmake` — maintaining two C++ code paths (one with dependencies, one without) wasn't worth it once `cmake` existed.

Generated `CMakeLists.txt` has a managed region, delimited by marker comments, that `gram add`/`gram remove` edit as text (no real CMake parser):

```cmake
cmake_minimum_required(VERSION 3.20)
project(main)

include(FetchContent)

# --- gram dependencies (managed, do not edit by hand) ---
# --- end gram dependencies ---

add_executable(main src/main.cpp)
```

## Two paths for an external cmake dependency

`gram add <dep> --module <m>` (cmake modules):

- **FetchContent** (default; `--url`/`--tag`) — for anything distributed as a plain git-clonable CMake project. Verified with olcPixelGameEngine. Writes a `FetchContent_Declare`/`FetchContent_MakeAvailable` block into the managed region.
- **vcpkg manifest mode** (`--vcpkg`, mutually exclusive with `--url`/`--tag`) — for anything with a maintained port, where the build itself (system deps, cross-platform patches) is nontrivial enough that you want someone else's port to have solved it. Verified with `fmt`. Writes/updates the module's `vcpkg.json` and a `find_package(<dep> CONFIG REQUIRED)` line. Needs `VCPKG_ROOT` set (clear error if it isn't, checked lazily — only when a module actually has a `vcpkg.json`).

Both cases: gram only wires *resolution*. `target_link_libraries` stays a manual edit — gram can't reliably guess the right CMake target name for an arbitrary external dependency. (Contrast with module references, below, where it can.)

`gram remove <dep> --module <m>` doesn't need to know which mechanism was used — it strips the marker block from `CMakeLists.txt` and, opportunistically, the entry from `vcpkg.json` if present, regardless of how the dependency got there.

### Bug: stale CMake cache silently ignored `VCPKG_ROOT`

`CMAKE_TOOLCHAIN_FILE` only takes effect on a build directory's **first** configure. A `build-linux/` already configured (e.g. before a module had any vcpkg dependency) silently ignores a toolchain file passed on a later `cmake -S -B` call — no error, no log, `find_package` just fails later with no trace of vcpkg having run at all. Fixed by always passing `--fresh` to the configure step: `gram` already regenerates `CMakeLists.txt`/`gram-build.ron` on every `add`/`remove`, so a full reconfigure on every build is consistent with the tool's own model, not just a patch.

### Gotcha, not a `gram` bug: `pkg-config` missing

Hit live testing vcpkg + `fmt` on this VM: the build failed with `CMAKE_MAKE_PROGRAM not set` / `CMAKE_C_COMPILER not set`, which looks like a missing toolchain. The real cause was buried in `vcpkg_installed/vcpkg/issue_body.md`: `fmt` built fine (debug + release) and failed only at its pkgconfig-fixup step because `pkg-config` wasn't installed. The surface error is misleading — worth remembering next time a vcpkg build fails with "compiler not set"-style errors after apparently getting past compilation.

## `gram run`

`gram run [--module <m>] [--target <t>] [-- <args>]` executes an already-built `kind: "runtime"` module's binary straight from `builds/<target>/` — it does **not** trigger a build first. `BuildConfig` gained a `main: String` field (set by `gram init` to the initial module's name) so `--module` can be omitted; omitting `--target` uses the host OS. Anything after `--` is forwarded verbatim to the binary via clap's `#[arg(last = true)]`, regardless of its position relative to gram's own flags — needed because a run target might parse its own subcommands (e.g. `gram run --module gram -- build`, running the self-hosted `gram` binary with `build` as *its* argument).

Deliberately **no `--ci`** on `run` — it's a dev-loop convenience for the person writing code; CI runs the `gram build --ci` artifact directly, never through `gram run`.

Removing the module currently set as `main` (`gram remove --module <main-module>`) clears `main` back to empty rather than leaving it dangling — otherwise a later bare `gram run` would fail with a confusing "module not found" instead of a clear "no main module set."

## `--target` on `init`/`add`/`build`, and a `gram build` correctness fix

`ModuleConfig.targets` (which platforms a module declares support for) used to be set once, automatically, to the host OS, with no way to change it except hand-editing `gram-build.ron`. Now:

- `gram init`/`gram add --module <new>` accept `--target linux,windows` (comma-separated) to set the initial list; omitted, defaults to the host OS as before.
- `gram add --module <existing> --target <t>` **appends** any not-yet-declared targets to an existing module — idempotent, no duplicates.
- Values are validated against an explicit allowlist (`linux`, `windows` — `macos` planned, rejected with a clear error until then), both here and on `gram build --target`.
- `gram build --target <t>` cross-builds a single target locally (`builds/<t>/`, not the `--ci` layout) — rejected if combined with `--ci`, which already sweeps every declared target.
- **Bug fixed**: `gram build` (no `--target`) always silently built for the host OS regardless of what a module actually declared in `targets`, so a module that only declared `["windows"]` would still attempt a native build on a Linux host with no signal anything was off. Now it warns (doesn't fail — the compiler will fail on its own if the target genuinely can't build) whenever the resolved target isn't in the module's declared list.

## Module references (`--module-ref`) — same-language cross-module dependencies

`gram add <module> --module <consumer> --module-ref` treats `<module>` as another module already in this project's `gram-build.ron`, not an external dependency:

- **cargo → cargo**: `cargo add <dep> --path <dep> --manifest-path <consumer>/Cargo.toml`. The `--path` argument is just the dependency's own directory name, unqualified — gram's module layout is always flat (every module is a direct child of the project root, and gram already runs with cwd = project root), so no relative-path computation is needed.
- **cmake → cmake**: inserts `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../<dep> ${CMAKE_CURRENT_BINARY_DIR}/<dep>)` into the managed dependency region, **plus** `target_link_libraries(<consumer> PRIVATE <dep>)` — appended after the target declaration (in its own `# gram:link:<dep> begin/end` marker block, since `target_link_libraries` needs the target to already exist, unlike everything else that can live in the pre-target dependency region). Unlike FetchContent/vcpkg, gram *does* know the exact CMake target name here — it generated it — so it wires the link automatically instead of leaving it manual.
- `gram remove` strips both blocks (dependency-region and the appended link block) if present — the link block is a no-op to look for on FetchContent/vcpkg dependencies, which never have one.

Validated before touching anything: the referenced module must already exist, can't be the consumer itself, must be `kind: "library"` (a `"runtime"` module has nothing meaningful to link against), and must be the same `lang` as the consumer — cross-language references (cmake depending on a cargo module, or vice versa) are explicitly rejected with a clear error, not attempted.

### Two adjacent bugs found while building this

Neither was reachable before `--module-ref` existed, since nothing had exercised `kind: "library"` for a `cargo` module in practice:

1. `save_cargo` ignored `kind` entirely — a `"library"` cargo module still got a plain `cargo init` (produces `src/main.rs`, a binary crate), so it had no library target for anything to actually reference. Fixed: `--lib` is passed when `kind == "library"`.
2. `compile_cargo` unconditionally expected a binary at `<manifest_dir>/target/debug/<name>` and tried to move it into `builds/` — which doesn't exist for a library crate, so `gram build` failed with "No such file or directory" on *any* project containing a cargo library module, even one that had nothing to do with the module actually being built. Fixed: a `"library"` cargo module just gets compiled, no move step.

### Verified end to end (not just "the file looks right")

- cargo↔cargo: a library module exporting a function, a runtime module calling it through the path dependency, real `cargo build` + run, correct output.
- cmake↔cmake: a shared-library module, a runtime module calling one of its functions through the auto-generated `target_link_libraries`, real `cmake --build` + run, correct output.
- Every validation guard (self-reference, missing module, cross-language, non-library target, `--module-ref` combined with `--url`) — each produces the intended error, not a crash or silent wrong behavior.
- `gram remove` on a module-ref dependency restores `CMakeLists.txt` to byte-identical original content (both blocks cleanly stripped).

### Bonus fix: `gram remove --purge`

Also added this session, smaller: `gram remove --module <m>` (no dependency name) stays config-only by default, matching prior behavior exactly — `--purge` additionally deletes the module's directory from disk. Deliberately opt-in, not opt-out: an ordinary `gram remove` should never be destructive to files you didn't ask it to touch.

## Open question from `wave-0-self-hosting.md`, now partially answered

That doc flagged `ModuleConfig.deps` as ambiguous between "external crate dependency" and "inter-module dependency" — both now genuinely share the same field (a module-ref dependency's name is pushed into `deps` exactly like an external one's). The list still doesn't structurally distinguish the two kinds — telling them apart means cross-checking a `deps` entry against `self.modules`' names. Not a problem in practice yet (nothing currently needs to filter one kind out), but worth keeping in mind if that ever changes.

## What's next

- **cmake ↔ cargo module references — deliberately not built here.** This is Wave 1's actual core problem (`gram-core` linked statically into the C++ runtime via `cxx` + Corrosion, per the project note's Wave 1 architecture), not a generalization of the same-language case above: it means bridging two different build systems with FFI codegen in between (`corrosion_import_crate` + `cxx_build`), not just linking two targets. Solving it now, generically, before there's a second concrete use case to generalize from, would be premature — it gets solved concretely when Wave 1 starts, in the exact `gram-core`/`gram-hot` shape already designed.
- Decide `deps`' dual meaning (see above) before it actually causes a problem.
- Task 0.5 (devlog Episode 0) is still the only original Wave 0 item left open.
