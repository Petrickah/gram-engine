# Wave 0 — Gram Build

Status as of 2026-09-09: Task 0.1–0.4 done. Task 0.5 (devlog Episode 0, not yet written) remains before Wave 1 (Hello Triangle).

## What `gram` does now

A single Rust binary runs two subcommands:

- **`gram init <name> [--lang cpp|rs]`** — creates `<name>/gram-build.ron` (a `BuildConfig` with a single `ModuleConfig`, named `main`) plus the matching Hello World source file (`src/main.cpp` or `src/main.rs`). The module automatically gets `targets: [<current OS>]` (`std::env::consts::OS`), no user input needed.
- **`gram build [--path <dir>] [--ci]`** — reads `gram-build.ron` (from cwd, or with `--path`, `chdir`s there first) and compiles each module:
  - without `--ci`: compiles **once**, for the current system, regardless of what's listed in `targets` → `builds/<target>/<bin>`.
  - with `--ci`: iterates **every** target in `targets` → `../builds/<project>_<version>_<target>/<bin>` (sibling to the project directory, not inside it).

Both languages supported now: `cpp` (via `g++`, or `x86_64-w64-mingw32-g++` for the `windows` target, with `-static-libstdc++ -static-libgcc` so the binary doesn't depend on DLLs at runtime) and `rs` (via `rustc`, `--target x86_64-pc-windows-gnu` for `windows`). Both actually verified to run under `wine`, not just to compile.

## `gram-build.ron` format

Serialized with `ron` + `serde`, with struct names included explicitly (`PrettyConfig::struct_names(true)`) so the file is self-descriptive when opened:

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
            targets: [
                "linux",
            ],
        ),
    ],
)
```

`deps` (dependencies between modules within the same project) exists in the schema but is unused — relevant only from Wave 1 onward, once `gram-core`/`gram-hot` actually depend on each other.

## Design decisions (and why)

- **Cross-compilation only with `--ci`.** A local `gram build` always stays scoped to the current system, no extra flag — there's no way to request cross-compilation outside `--ci`. Decided now because Wave 0 has no native dependencies (no Vulkan yet); to be revisited at Wave 1, once Vulkan's platform-specific dependencies probably won't cross-compile as cleanly. See `CLAUDE.md` → Non-goals.
- **`--ci` layout simplified from the original plan.** The kanban originally called for `builds/<project>/<date>-<commit>/<platform>/<binary>` (meant to avoid overwriting successive builds from a real pipeline). `<date>-<commit>` was dropped, keeping only `<project>_<version>_<target>` — there's no CI pipeline actually consuming these artifacts yet (see the "No CI/CD until an explicit Personal Builds stage" non-goal), so that granularity is complexity with no one to serve it right now.
- **`--path` does a `chdir`, not path-prefixing.** All the build logic (reading `gram-build.ron`, a module's `src`, `builds/`) is written relative to "current directory = project directory." Simpler to change the cwd once at the start of the command than to thread `path` through every function that touches the filesystem.

## What's next

- Task 0.5 — devlog Episode 0 (article for `petretiberiu.dev`), not yet written.
- Wave 1 — Hello Triangle: window + Vulkan + triangle, plus the `gram-core`/`gram-hot` split. `deps` on `ModuleConfig` and cross-compilation will both need revisiting there.
