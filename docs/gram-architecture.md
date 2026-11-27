# Gram Build — architecture and developer guide

How `gram` works inside, where to change what, and why the odd parts are odd. For the command reference see `README.md`; for history see `wave-0-*.md`. If this document and the code disagree, the tests in `gram/gram-cli/tests/` describe the behavior that's actually guaranteed.

## The model in one paragraph

A project is a `Gram.ron` file: a `BuildConfig` with a list of `ModuleConfig`s. `gram` doesn't compile anything itself. It reads the project, then asks each module's **backend** (one per `lang`: `cargo`, `cmake`, `gram`) to scaffold, build, or edit dependencies, by running the real tool (`cargo`, `cmake`) and editing that tool's own files (`Cargo.toml`, `CMakeLists.txt`, `vcpkg.json`). Everything runs with the project root as the current directory.

## Crates and files

```
gram/                         a nested gram project and a cargo workspace
  Gram.ron                    gram-cli (runtime, main) + gram-common (library)
  gram-cli/src/main.rs        parses the CLI, calls gram-common, prints the outcome
  gram-cli/tests/             command tests (see "Tests")
  gram-common/src/
    commands/                 clap structs + one thin function per subcommand
    config/mod.rs             BuildConfig: open/write, modules, build/run, add/remove dependencies
    config/module.rs          ModuleConfig: per-module build, run, save
    lang/mod.rs               ModuleBackend trait, backend_for, DependencySpec, ResolvedModule
    lang/cargo.rs             Cargo backend
    lang/cmake.rs             CMake backend
    lang/gram.rs              Gram backend (nested projects)
    lang/vcpkg.rs             vcpkg manifest helpers, used by the CMake backend
    target.rs                 supported targets, what the host can build, cargo triples
    directory.rs              Directory: writing files relative to a root
    vscode.rs                 .vscode/settings.json (rust-analyzer.linkedProjects), merged
```

The split is by responsibility, not by command: `build`, `add` and `remove` all touch both languages, so per-command files would scatter the CMake code across all of them.

## Data model

```ron
BuildConfig(
    name: "Game",
    version: "0.1.0",
    main: "Runtime",              // default for `gram run`; also names the main executable
    modules: [
        ModuleConfig(
            name: "Runtime",
            lang: "cmake",            // "cargo" | "cmake" | "gram"
            kind: "runtime",          // "runtime" | "library" | "dynamic"
            src: "Runtime/CMakeLists.txt",   // the module's manifest; its directory is the module's
            deps: [ Module(name: "GramEngine") ],
            targets: [ "windows", "linux", "macos" ],
        ),
    ],
)
```

- **`DependencySpec`** records where a dependency comes from: `Registry { name }` (crates.io), `Git { name, url, tag }` (FetchContent), `Vcpkg { name }`, `Module { name }` (another module of the project). It's serialized as-is, so the origin is visible in `Gram.ron`. `DependencySpec::new` turns the `gram add` flags into one and rejects invalid combinations; each backend accepts the variants it supports and rejects the rest.
- A module depends on a given **name** at most once, whatever the source (`has_dependency` compares names).
- **`ResolvedModule`** is a `Module { name }` looked up in the project: name, lang, kind, `dir` (relative to the root) and `rel_dir` (relative to the consumer module). It's computed when needed and never written to `Gram.ron`, which keeps only the name. A module's location is a property of the module (`src`), not of whoever references it.
- `lang` and `kind` are still plain strings. Turning them into enums would let the compiler find every place a new variant needs handling (see "Open directions").

### Nested projects

A module with `lang: "gram"` has another `Gram.ron` as its `src`. `BuildConfig::open` reads it recursively and appends its modules to the **same flat list**, with `src` prefixed by the nested directory, and remembers in a `#[serde(skip)] project` field which file each module came from. Everything after `open` (build, references, backends) sees one list of modules with root-relative paths. `BuildConfig::write` reverses it: each `Gram.ron` gets back only its own modules, with `src` relative to it again.

Consequences:
- Module names are unique across the whole tree (`open` fails on a duplicate), so `Module(name: "GramEngine")` from `Game/` finds the root's `GramEngine` without qualified names.
- A `gram` module compiles nothing, takes no dependencies and can't be run; removing it removes its modules too.
- Always save through `BuildConfig::write`, never by serializing `BuildConfig` straight to a file.

## Command flows

- **`init`**: builds a `BuildConfig` with one module, sets `main`, writes `Gram.ron`, asks the backend to `scaffold` the module, writes `.vscode/settings.json`.
- **`add`** (`BuildConfig::add_dependency`), in this order, so a failure halfway doesn't leave an orphan:
  1. create the module if it doesn't exist (scaffold + write `Gram.ron` immediately), or append new targets;
  2. build the `DependencySpec` from the flags; no dependency name means stop here;
  3. if the module already has that name: return `AlreadyPresent`, run nothing;
  4. `Module` dependencies: resolve, check it's a `library` of the same `lang`, then `backend.add_module_ref`; anything else: `backend.add_dependency`;
  5. write `Gram.ron`, and return an `AddOutcome`. The core reports what happened; `main.rs` decides what to print.
- **`remove`**: with a dependency name, `backend.rm_dependency`; without, removes the module (and a `gram` module's children) from the config, optionally `--purge`s its directory. A missing module is an error.
- **`build`** (`BuildConfig::build` → `ModuleConfig::build` → `build_target`): for every module in list order, for every target (`--ci`: all declared targets the host can build; otherwise the host or `--target`), compute the output path `build/<project>/<target>/<prefix><artifact_name><ext>` (`--ci`: `build/<project>_<version>_<target>/`), resolve the module's references, call `backend.compile`. `artifact_name` is the project's name for a project's main runtime module (`Game`, `gram`) and the module's own name otherwise.
- **`run`**: finds the already-built artifact by the same naming rules and executes it. No build.

`--path` on `build` changes the process's cwd once, at the start. That's the only `set_current_dir` in the code; nothing else may change it (an earlier helper did, and broke unrelated code that ran after it).

## Backends

```rust
pub(crate) trait ModuleBackend {
    fn manifest_file(&self) -> &'static str;                 // "Cargo.toml", "CMakeLists.txt", "Gram.ron"
    fn scaffold(&self, module, project) -> io::Result<()>;
    fn compile(&self, module, refs: &[ResolvedModule], out, target) -> io::Result<()>;
    fn artifact_naming(&self, module, target) -> (&'static str, &'static str);   // (prefix, extension)
    fn add_dependency(&self, module: &mut ModuleConfig, dep: &DependencySpec) -> io::Result<()>;
    fn rm_dependency(&self, module: &mut ModuleConfig, dependency: &str) -> io::Result<String>;
    fn add_module_ref(&self, module, resolved: &ResolvedModule) -> io::Result<()>   { Err(unsupported) }
    fn rust_analyzer_manifest(&self, module) -> Option<&str>                          { None }
}
```

Backends are zero-sized structs (`Cargo`, `CMake`, `Gram`): behavior only, the data stays in `ModuleConfig`. `backend_for(lang)` is the only place that matches on the `lang` string.

### Cargo

- Delegates to `cargo init`/`add`/`rm`/`build`. Only `Registry` dependencies; references are path dependencies (`cargo add <name> --path <dir>`, where `dir` is root-relative because cargo resolves it against the cwd).
- `compile` runs `cargo build --message-format=json-render-diagnostics` and takes the executable's path from cargo's own report (`find_executable`), so workspaces and multi-bin packages work without guessing `target/debug/<name>`.
- Windows cross-builds use `--target x86_64-pc-windows-gnu`; the mingw linker and static CRT come from the vendored `.cargo/config.toml`.

### CMake

- `scaffold` writes a `CMakeLists.txt` (+ `src/main.cpp`, or `include/<name>/<name>.hpp` + `src/<name>.cpp` for a library) with a **managed region** between two marker lines (`DEPS_BEGIN`/`DEPS_END`). `add`/`remove` edit only that region, as text, one `# gram:dep:<name> begin/end` block per dependency. There's no CMake parser, and nothing outside the markers is ever touched.
- `Git` → a `FetchContent_Declare` + `FetchContent_MakeAvailable` block. `Vcpkg` → `find_package(<name> CONFIG REQUIRED)` + an entry in `vcpkg.json` (see `vcpkg.rs`). Both only resolve the dependency: `target_link_libraries` stays manual, because gram can't know an external package's target names. The generated `find_package` is a guess and sometimes needs a hand fix (vcpkg's `vulkan` resolves through CMake's built-in `FindVulkan`).
- `Module` → `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/<rel_dir> ...)` + `target_link_libraries`, which gram *can* generate, since it named the target itself.
- `compile` configures `<module dir>/build-<target>` with `--fresh` every time (settings like `CMAKE_TOOLCHAIN_FILE` only apply on a build dir's first configure, and gram rewrites the files on every add/remove anyway) and `CMAKE_EXPORT_COMPILE_COMMANDS=ON` (IntelliSense reads that instead of a separate IDE configure).
  - With a `vcpkg.json`: `CMAKE_TOOLCHAIN_FILE` from `VCPKG_ROOT`. vcpkg reads only the top-level manifest, so for a module with references gram merges the referenced modules' manifests into `<build dir>/vcpkg-manifest/vcpkg.json` (duplicates keep the fuller form, features are united) and passes `VCPKG_MANIFEST_DIR`.
  - Windows: `CMAKE_SYSTEM_NAME=Windows`, the mingw compilers, `-static-libgcc -static-libstdc++ -static` so the `.exe` runs standalone, and `VCPKG_TARGET_TRIPLET=x64-mingw-static` (vcpkg otherwise picks the MSVC `x64-windows` triplet for any Windows target).
  - After building, the artifact is copied to `out`: an executable under gram's name, a library under its real file name (static or shared, gram looks for both). Then `<build dir>/shaders/` is copied next to it.

### Gram

Nested projects (see above): `scaffold` writes an empty `Gram.ron`, `compile` does nothing, dependencies are rejected.

### vcpkg

Not a backend: vcpkg is a dependency source for CMake modules, not a module language. `vcpkg.rs` only holds manifest helpers.

## Conventions

- **cwd is the project root**, for gram and for every tool it starts. Paths in `Gram.ron` and in memory are root-relative. The one exception is what a tool wants relative to the consumer (CMake's `add_subdirectory`), which is why `ResolvedModule` carries both `dir` and `rel_dir`.
- **Paths written into files use `/`** (`slash_path`), whatever the host; `Path::display` would give `\` on Windows.
- **Errors** are `io::Error` with a message that says what to do next ("create it first, e.g. `gram add --module …`"). The CLI prints `Error: …` and exits with 1.
- **Progress output** is a `gram <step> (key=value, …)` line per step; warnings go to stderr and never fail the command (an undeclared target, a duplicate dependency, a skipped `--ci` target).
- **Non-destructive by default**: `remove` only edits `Gram.ron` unless `--purge`.
- **`.vscode/settings.json` is merged, not overwritten**: gram owns `rust-analyzer.linkedProjects` and leaves every other key alone.

## Extending gram

### A new module language

1. `lang/<name>.rs` with a zero-sized struct and `impl ModuleBackend for <Name>`. The compiler tells you which methods are missing.
2. One line in `backend_for`.
3. If it supports references, override `add_module_ref` and use `ResolvedModule` (never rebuild paths from the module name).
4. If its artifact isn't a single executable or library, see "Known limits": `compile` gets one `out` path.
5. Tests: a new file in `gram-cli/tests/`, using `Sandbox`.

### A new dependency source

1. A variant in `DependencySpec`, its flag in `commands/add.rs`, the combination rules in `DependencySpec::new`, and a `name()` arm.
2. Handle it (or reject it) in each backend's `add_dependency`; the `match` makes forgetting one a compile error.
3. Existing `Gram.ron` files keep working as long as the old variants don't change shape.

### A new target

`SUPPORTED_TARGETS`, `buildable_on_host` and `cargo_cross_triple` in `target.rs`, then each backend's `artifact_naming` and cross-compile flags.

## Tests

`gram-cli/tests/` has one file per command plus `nested.rs`. Read `support/mod.rs` first; it's short. Every test follows the same three steps, prepare → one `gram` command → check, and only looks at what a user sees: files written and output printed. That's what keeps the tests valid across internal restructuring.

- `Sandbox` gives each test its own temp directory and runs the real `gram` binary in it. A failing test keeps its sandbox and prints the path, so you can look at what gram produced.
- `.succeeds()` / `.fails_with("message")` print all of gram's output on failure.
- `project.config()` reads `Gram.ron` into a copy of the format kept *in the tests*, so changing the format means editing the tests on purpose.
- vcpkg is faked with an empty toolchain file: no network, nothing installed.
- The `build`/`run` tests need cmake and a C++ compiler; one test needs crates.io and is `#[ignore]`d. Windows cross-builds aren't tested.

Run them with `cargo test --manifest-path gram/Cargo.toml`. A bug fix starts as a failing test.

## Known limits and stopgaps

- **One artifact per module.** `compile` gets a single `out` path. Compiled shaders reach the executable through a hardcoded copy of `<build dir>/shaders/` in the CMake backend; the plan is for a module to declare extra outputs (e.g. `outputs: ["shaders"]`).
- **Only direct references are merged for vcpkg.** If a module references one that references a third, the third's manifest isn't merged. A `platform` conflict for the same port isn't reconciled: the first manifest wins.
- **A referenced cmake library is configured twice**: once as its own module (`GramEngine/build-<target>`), once inside its consumer through `add_subdirectory`. Correct, just slower. A single top-level CMake project for all C++ modules would remove this, the manifest merging, and the `CMAKE_BINARY_DIR` trick for shaders, at the price of gram treating C++ modules differently from the rest.
- **No cross-language references** (cmake ↔ cargo). Wave 1 doesn't need one: the C++ executable loads the Rust `cdylib` at runtime instead of linking it.
- **`kind` and `lang` are strings**, compared with `==` in a few places; a typo compiles fine.

## Open directions

- `crate-type = ["cdylib"]` support for cargo modules and copying the library next to the executable (Task 1.3), probably as a distinct module kind for things that are built but not linked.
- The `.ron` component schema → Rust struct + C++ struct + descriptor table generation step (after Task 1.4). See `wave-1-rust-gameplay-design.md`.
- Enums for `kind`/`lang` in `Gram.ron`, like `DependencySpec` (another one-time migration of the files).
- Precompiled binaries as a dependency source (`--bin <url> --for <target>`, discussed for DXC; not needed since DXC comes with the Vulkan SDK).
- `gram test`, running `cargo test` / `ctest` per module.
- **Macros** (inspired by an internal Ubisoft tool): named recipes in `Gram.ron`, each a list of steps run in order, where a step is an existing gram command, so a macro knows nothing about CMake or Cargo. For example:
  ```ron
  macros: {
      "play":    [ Build(target: "macos"), Run(module: "Game") ],
      "windows": [ Build(target: "windows") ],
  }
  ```
  Invocation is undecided: `gram run --macro play`, or a separate subcommand (`gram do play`) so that `run` keeps meaning "execute an already-built binary, no build". A step is a plain Rust module in `gram-common`, looked up by name like a backend; it moves to its own crate only once it outgrows that (heavy dependencies, a lot of code of its own). Callers don't change, since they find it by name. User-defined steps, the equivalent of that tool's C# scripts, could start as an `Exec(command, args)` step that runs an external program, long before anything like a plugin system. An editor button later is just a macro.
- **When to split `gram-common` into crates**: not by command (commands cut across languages), and not by language yet (the backends already are separate files, and a crate boundary would only add `pub` API and manifests at this size). Split a piece out when it brings heavy dependencies of its own, when code outside gram needs it, or for third-party plugins. The likely first one is the crate holding the `.ron` component schema types (e.g. `gram-schema`), used by the codegen step and later possibly by the engine or editor.
