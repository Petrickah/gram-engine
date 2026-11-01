# Wave 1 — Rust gameplay layer over a C++ engine (design)

Status as of 2026-10-31: **adopted, Task 1.3 done** (loader, automatic reload, panics caught at the boundary) (see *Implemented so far*). Originally: Proposed 2026-10-21, adopted 2026-10-28 together with schema-driven components (see *Schema-driven components*) and a narrower Wave 1 proof of concept: one component with a text field, edited in ImGui, no sphere on screen. `CLAUDE.md` and Tasks 1.3/1.4 in `.gram.kanban.md` have been updated to match. Statements about third-party engines and platform behavior come from memory; re-check them before depending on them.

## The idea in one line

**All state lives in C++; the reloadable Rust library holds only logic.** C++ owns the objects, their IDs and their reference counts. The Rust `cdylib` receives a pointer to that state and a table of host functions on every call, and wraps them in a few safe types (`Handle`, `Reference`, an Object Manager) so that gameplay code is ordinary safe Rust.

This is the "state in the host, code in the DLL" pattern known from Handmade Hero, used across a language boundary.

## Why move away from the original plan

The original plan put the `bevy_ecs` World in a static Rust crate (`gram-core`) and the systems in a reloaded `cdylib` (`gram-hot`), so that `TypeId`s stay stable across reloads. `TypeId` is only part of the problem. What actually breaks a reload is anything in surviving memory that points into the unloaded library:

- **Drop glue and vtables.** A component type defined in the DLL has its `Drop` code in the DLL. If the static World holds such a component, destroying it after `dlclose` jumps into unmapped code. Hence the rule that every type must live in `gram-core`, which is easy to break by accident.
- **Layout changes.** Add a field to a type defined in the DLL, recompile, reload: the data already in memory has the old layout.
- **`TypeId` stability.** `bevy_ecs` indexes components by `TypeId`, which is not guaranteed stable between compilations.
- **Build complexity.** A static Rust lib linked into C++ (Corrosion + `cxx`) *and* a `cdylib`, with `gram-hot` carrying its own monomorphized copy of `bevy_ecs` that must match `gram-core`'s exactly.

In the design below no Rust type crosses a reload: shared data is `#[repr(C)]`, in host memory, and the DLL leaves no function pointers or `Drop`s behind.

## Architecture

```
GramEngine (C++, static lib)   renderer, objects/ECS, assets, reference counts, ID table
Game exe   (C++)               main(), links GramEngine, loads / reloads the Rust library
game-logic (Rust cdylib)       stateless Object Manager + Handle/Reference + gameplay code
gram-core  (Rust rlib)         the engine's side of the contract as Rust types (HostAPI, LogLevel)
```

The library was called `gram-hot` while this was being designed; it's a game module, so it's named after the game (`game-logic`) and lives in the `Game` project. `gram-core` came back with a different job than in the original plan: types only, no state, no exported functions, linked into `game-logic` like any crate.

- Matches the spirit of `CLAUDE.md` ("C++ owns `main()` and the renderer core ... Rust is the reloadable gameplay layer"). What changes: `gram-core` disappears, and the world's state moves from `bevy_ecs` to C++.
- The boundary is plain `extern "C"` plus `#[repr(C)]` structs in both directions. No `cxx`, no Corrosion static link.

## Implemented so far (2026-10-31)

Built up in steps: C++ only, then a statically linked Rust `staticlib`, then the dynamic library. The FFI code is the same in the static and the dynamic case; only how C++ finds a function changes (the linker vs `dlsym`).

- **Two tables, one symbol.** `HostAPI` is filled by C++ (`g_Host` in `Gram/Scripting/HostAPI.hpp`, today only `Log(LogLevel, const char*)`). `GameAPI` is a `static` in Rust (`version`, `on_load`, `on_unload`, `greet`). The library exports only `game_get_api() -> *const GameAPI`; the functions in the tables aren't `#[no_mangle]`, nobody looks them up by name.
- **Load sequence** (`GameAPILoader`, an application layer in `Game/GramRuntime/src/GameAPI.{hpp,cpp}`): copy `<exe dir>/libgame_logic.dylib` to `<exe dir>/hot/libgame_logic_<N>.dylib`, `DynamicLibrary` (`dlopen`/`LoadLibraryW`) on that copy, `FindSymbol("game_get_api")`, check `Version == GameAPIVersion`, `OnLoad(&g_Host)`, which stores a *copy* of the host table in the library's `OnceLock` and returns the `GameAPI` to use. Any failure leaves the game running without Rust logic (the Greet button is disabled). Exit: `OnUnload()`, drop the `GameAPI` pointer (it points into the library), then unload.
- **Copy-on-load + reload.** The original is never loaded: on Windows a loaded DLL is locked and the build couldn't replace it, on macOS writing over a mapped library can crash the process. Each load uses a *new* name (`_1`, `_2`…) because macOS often doesn't really unload a Rust dylib on `dlclose`, and a `dlopen` of a path already in memory returns the old image (old statics, `HOST` already set). `Reload()`: `OnUnload()`, drop the `GameAPI` pointer, remove the previous copy, copy, unload the old library, load the new one, `game_get_api` → version check → `OnLoad`. Triggered by an Inspector button, and automatically: every 0.5 s `OnDrawFrame` compares the original's `last_write_time` with the one read before the last copy (read *before* copying, so a build landing mid-copy triggers one more reload). `hot/` is emptied at startup and exit. On gram's side every artifact is written as `<name>.partial` and renamed over the old one, so the watcher never reads a half-written library (and macOS doesn't kill a running `Game` or `gram` whose file was rewritten in place).
- **Every loaded library has its own statics.** An `rlib` dependency is copied into each library that uses it, so a `static` in `gram-core` would exist once per library. That is exactly why the host table can live in `gram-core` (`static HOST: OnceLock<HostAPI>`, private, set only by `gram_core::init`): it ends up inside `game-logic`'s image, and is initialized by calling *that* library's `on_load`. A reload works the same way: the new copy starts with empty statics and gets the table again. The `OnceLock<HostAPI>` doesn't break rule 2 below: it's a copy of host function pointers, rebuilt on every load, not game data.
- **Data**: `Greeting` (`char[64]` / `[u8; 64]`) lives in `RuntimeApplication`; Rust only gets a pointer for the duration of a call. A hand-written `ComponentDescriptor` drives a generic ImGui inspector (one `switch` per field type, not per component).
- **Fixed-size types only** across the boundary (`uint32_t`/`u32` for `LogLevel`, pointers, `bool`), so the layout is the same on every platform. `HostAPI` itself has nothing platform-specific; only `DynamicLibrary` does.
- **`gram-core` API** (2026-11-01): `init(*const HostAPI) -> bool` (null pointer or a second call → `false`), `log(LogLevel, &str)` (`CString`, never panics, a no-op before `init`) and `info!`/`trace!`/`warn!`/`error!` (`format!`-style `macro_rules!`, `$crate::log`), `guard`. `game-logic` no longer touches the host table: `on_load` is `init` + `guard(|| &GAME)`.

Verified 2026-10-31: with `Game` running, changing `greet`'s log message and running `gram build` is picked up without pressing Reload, while the text edited in the inspector stays.

Panics (2026-11-01): `gram-core::guard` wraps the whole body of every exported function in `catch_unwind` (`AssertUnwindSafe` when it captures `&mut` host data: after a panic the component may be half-written, which is accepted), and the hook `gram_core::init` installs logs `file:line:col: message` (`&str` or `String` payload) through `HostAPI::Log`. The hook itself contains nothing that can panic: a panic inside it aborts the process. Verified with `panic!("text")`, `panic!("{}", 42)` and an out-of-bounds index (through `std::hint::black_box`, otherwise rustc rejects it at compile time): logged, the game keeps running. Not done yet: `ObjectId`/`Handle` (Rust still gets a raw pointer to the component).

## State ownership rules

1. **Everything that must survive a reload lives in C++:** objects, components, the ID table, reference counts.
2. **The Rust library has no state of its own.** No `static`s or `thread_local`s holding game data. Anything cached is rebuilt in `on_load`.
3. **Rust wrapper values (`Handle`, `Reference`) are temporary views.** None may exist at reload time. When C++ needs to remember "entity X", it stores an ID, never a Rust object.

## Reload sequence

```
host                                   gram-hot (cdylib)
────                                   ─────────────────
load(copy of gram-hot)
on_load(&state, &host_api)     ──►     build the Object Manager from host_api (no allocation of game state)
loop:
  update(&state, dt)           ──►     gameplay through Handle / Reference
on file change:
  on_unload(&state)            ──►     drop every cached Rust value (Handles, References, caches)
  unload; load(new copy)
  on_load(&state, &host_api)   ──►     same pointer, new code
```

Nothing is "recovered" after a reload. The new code gets the same `state` pointer on its next call.

## Host API

A `#[repr(C)]` struct of function pointers that C++ fills and passes to `on_load`. Rust never links against C++ symbols directly, which keeps the DLL loadable on its own.

```rust
#[repr(C)]
pub struct HostApi {
    pub log:      extern "C" fn(level: u32, msg: *const u8, len: usize),
    pub resolve:  extern "C" fn(id: ObjectId) -> *mut c_void,  // null if the ID is stale
    pub add_ref:  extern "C" fn(id: ObjectId),
    pub release:  extern "C" fn(id: ObjectId),
    // spawn / destroy / input / ... added as needed
}
```

Version the struct (a `version` or `size` field first) so a library built against an older host fails loudly instead of calling the wrong slot.

## The Rust glue types

All `unsafe` lives in these types. Gameplay code above them doesn't use any.

- **`ObjectId` = index + generation** (a *generational index*), `#[repr(C)]`, `Copy`. When an object is destroyed and its slot reused, the generation changes, so stale IDs are detected instead of aliasing a new object.
- **`Handle<T>`** wraps an `ObjectId` and does **not** keep the object alive (weak). Every access asks the host to resolve it and gets `Option` back. Fits entities, and assets that may be streamed out.
- **`Reference<T>`** wraps an `ObjectId` and **keeps the object alive** (strong). `Clone` calls `host_api.add_ref`, `Drop` calls `host_api.release`. Its `Drop` code lives in the DLL, which is fine: `Reference`s only exist in Rust memory and are all dropped before unload (rule 3).
- **`ObjectManager`** is the only code that touches `HostApi`. It hands out `Handle`s and `Reference`s and is rebuilt on every `on_load`.

The asset side mirrors Bevy's strong/weak asset handles. The engine-object side resembles godot-rust's `Gd<T>` over Godot's `RefCounted` vs manually managed objects.

### Aliasing: the part that keeps safe code actually safe

Rust assumes there are never two `&mut` to the same object. Two `Handle`s to the same C++ entity could break that and cause undefined behavior without any `unsafe` in gameplay code. Pick one of these when implementing:

- **Closure access:** `handle.with_mut(|e| ...)`. The reference can't escape the closure. Simple, but still allows nesting two `with_mut` calls on the same ID, so pair it with a check.
- **Runtime borrow tracking**, like `RefCell`: `bind()` / `bind_mut()` return guards and panic on a conflicting borrow. godot-rust does this. Costs a flag per object (it can live on the C++ side next to the reference count).

## Data layout across the boundary

- Every shared type exists twice, in C++ and in Rust, with the same layout. Check it on both sides: `static_assert(sizeof(T) == N && alignof(T) == M)` in C++, `const _: () = assert!(size_of::<T>() == N);` in Rust.
- Neither side is the source: both are generated from a `.ron` schema (next section). Until the generator exists, write by hand exactly what it would produce.
- **Changing a shared layout needs a full restart** (or a migration step). A reload only replaces code, never the shape of data already in memory. This is the same limit every hot-reload system has.

## Schema-driven components

A component is described once, in a `.ron` schema, and `gram build` generates both sides from it. Same workflow as Anvil's `.mold` files (which generated the C# editor-side model and the C++ side from one definition):

```ron
Component(
    name: "Greeting",
    fields: [
        Field(name: "text", ty: Str(64)),
    ],
)
```

Generated from it:

- **Rust**: a `#[repr(C)]` struct, written into the module's cargo package (a `.rs` file, not a crate).
- **C++**: the matching struct, with the `static_assert`s on size, alignment and offsets.
- **A static descriptor table** (component name, fields with name / type / offset): what lets an ImGui inspector show any component generically, without knowing `Greeting` at compile time.

What changes need what:

| Change | Rebuild | Restart |
|---|---|---|
| A schema (`.ron`): new component, new field | C++ executable and the Rust library (both sides are generated) | yes |
| Rust gameplay logic | the `cdylib` only | no, reload (copy-on-load) |
| C++ code | the executable | yes (an editor could later hide this by restarting the game process itself) |

Notes:

- **Strings**: `String` is neither `#[repr(C)]` nor allocated by the host. For now a schema string is `Str(N)`, a fixed inline buffer (`[u8; N]` / `char[N]`). C++-owned strings behind an ID can come later.
- **Inspector**: selecting an entity shows the cargo module it comes from, then one collapsing header per component (with the `.ron` it's defined in) and its fields, read from the descriptor table.
- **Instances**: Rust can read an instance from a `.ron` file and hand it to C++ (`add_component(entity, component, const void* data)`, copied into host memory). From then on Rust only reaches it through an `ObjectId`, never a stored pointer: host storage may move when it grows.
- Later, a file watcher triggers the reload; when there's an editor, it loads the game executable / the Rust library the same way. Not needed for the proof of concept.

## Loading components from disk (Wave 3+, a map for later)

Not needed until world serialization, but it uses the same descriptors as the inspector, so it shapes how they're designed now. This is the general pattern behind asset databases with versioned types: Anvil's single SQLite "Big File" with `PropertyDescriptor`s and a deprecation manager, Unreal's `.uasset` with property tags. It's a description of the pattern, not of how either engine implements it.

A stored object goes through four stages before gameplay code can use it:

1. **On disk: data plus its description.** An object is a blob (or rows) plus the version of its type when it was saved. Fields are identified by their descriptor (name or ID, type), never by a C++ offset. That's what lets a tool with no knowledge of the C++ class (an editor in another language, a migration script) read and write it.
2. **Generic read, before the final object exists.** The loader doesn't know `Greeting`. It walks the saved descriptor and reads values into an intermediate form: an int, a `Str(N)`, a reference to another object (stored as an ID).
3. **Into the object, migrating if the version changed.** Same version: the bytes can go straight into host storage. Older version: fields with the same name and type are copied automatically, and only what was renamed, split or retyped needs hand-written migration code. That code reads the old values through the old descriptor and writes the new layout. This is why a saved world must carry each component's descriptor, or at least its version and a way to find that descriptor. Saving only the struct's raw bytes makes migration impossible.
4. **Post-load, strictly last.** References are only IDs when read, because the referenced object may not be loaded yet. They're resolved once everything they depend on is in memory. Derived values are computed and validation runs here too. Until this stage finishes, the object exists but isn't ready, and the API has to say so: a lookup returns `Loading` / `Ready` / `Failed`, never a half-built object that looks normal.

Most of the hard bugs live between stages 2 and 4. Gameplay sees an object that looks fine while a reference is still unresolved, a field was just migrated or a derived value isn't computed yet. They depend on load order, and the logic causing them sits in the loading system, not in the class being debugged. Keeping the stages explicit, each in its own function, with the "not ready" state visible in the API, is the main defense.

What already exists or is planned that this builds on:

- **Descriptors** (the schema-generated table): used by the inspector now, by stages 1–3 later.
- **The schema as the single source**: it can carry a `version`, generated into the descriptor.
- **Generational `ObjectId`s**: a reference can be an ID before its object is ready, and stays safe to hold after the object is gone.
- **"Schema change = rebuild + restart"**: in memory nothing is ever migrated; migration is only a disk → memory concern.

### Related: who controls the shared layout

Today Rust reads components through a mirrored `#[repr(C)]` struct, so the layout must be identical on both sides: no virtual functions, no C++-only fields. Two alternatives keep the same schema and only change what `gram build` generates on the Rust side:

| Option | What Rust sees | C++-only fields | Cost |
|---|---|---|---|
| 1. Identical layout (current) | the whole struct, a plain slice | no; they go in a separate C++-only component of the same entity | simplest, fastest |
| 2. Shared part first, plus a stride | `struct Greeting { GreetingShared shared; /* C++ fields */ };` Rust gets `(ptr, count, stride)` and reads only `shared` | yes, after the shared part | iteration with a variable stride, not a plain slice |
| 3. Offsets from the descriptor | a generated view that takes field offsets from the host at `on_load` and reads `ptr + offset` | yes, anywhere | slightly slower; C++ alone owns the layout, as with an editor that only knows descriptors |

Option 1 is enough for `Greeting` (Task 1.4). Option 3 is the closest to a descriptor-driven editor model, and reuses the table stages 1–3 need anyway.

## Platform pitfalls

- **Windows locks a loaded DLL**, so the compiler can't overwrite it. Load a *copy* (`gram-hot-1.dll`, `gram-hot-2.dll`, …) and build over the original.
- **macOS: Rust `thread_local`s with destructors can make `dlclose` a no-op**, so the old code silently stays loaded. One more reason for rule 2. Verify on the current toolchain.
- **Panics must not unwind across `extern "C"`.** Depending on the Rust version and ABI this aborts or is undefined behavior. Wrap every exported function body in `std::panic::catch_unwind` and report failure to the host through the return value.

## Build

- A single `cargo` module of kind `dynamic` (added 2026-10-31): `gram add --kind dynamic` scaffolds it with `crate-type = ["cdylib"]`, and `gram build` copies the `.dll`/`.so`/`.dylib` next to the executable under cargo's file name (`libgame_logic.dylib`).
- Corrosion is no longer needed to link Rust into C++. The C++ side only `dlopen`s/`LoadLibrary`s the result.

## Open questions

- **ECS in C++:** write a small one (component arrays + generational IDs), or use EnTT? Writing it is a real learning task; EnTT is a known quantity.
- **Ergonomics vs `bevy_ecs`:** gameplay loses typed queries and system parameters. A thin query API over `HostApi` (for example "iterate all entities with component X") may be enough for Wave 1/2.
- **Threading:** assume single-threaded gameplay for now. Multi-threaded access changes the aliasing story.
- **Is the original plan still preferable?** Yes, if writing gameplay with `bevy_ecs` in Rust is itself the goal. Then the rule is strict: every type in `gram-core`, nothing with `Drop` or a vtable defined in `gram-hot`.

## Impact on the plan (applied 2026-10-28)

- **`CLAUDE.md`, Wave 1 description:** the `gram-core` / `gram-hot` split and the `cxx` + Corrosion mention replaced with this architecture.
- **Task 1.3:** "Rust `cdylib` + host loader: C++ loads `gram-hot` (copy-on-load), calls `on_load` / `update` / `on_unload` through `extern "C"`, panics caught at the boundary."
- **Task 1.4:** the proof of concept is a `Greeting` component (one `Str(64)` field) on one entity, shown and edited in an ImGui inspector; Rust reads or changes it through a `Handle`; a reload picks up new logic while the edited text survives. **No sphere and no Vulkan buffers in Wave 1**: the ImGui layers already give a place to see the data.
- **Sphere moves to Wave 2 (Task 2.1):** first a sphere owned by C++ in the compute ray tracer, then driven from Rust as a real entity. `wave-1-2-sphere-rendering-notes.md` keeps its rendering notes; its "Task 1.4" part (fullscreen triangle + push constants) is no longer planned for Wave 1.
- **ImGui (Task 1.y):** unaffected. The ImGui context already stays on the C++ side.

## Suggested order

1. Host loader + `extern "C"` lifecycle with an empty Rust library (copy-on-load, reload on file change, `catch_unwind`).
2. `HostApi` with `log` only: prove a call in each direction survives a reload.
3. `ObjectId` + ID table in C++, `Handle<T>` in Rust, one entity with a `Greeting` component (hand-written `#[repr(C)]` struct + descriptor), shown and edited in an ImGui inspector. A reload keeps the edited text.
3b. The `gram build` step generating the Rust / C++ / descriptor code from the `.ron` schema, once step 3 shows what it must produce.
4. `Reference<T>` once something needs to be kept alive (assets).
5. Aliasing guard (closure access or borrow flags) before more than one gameplay system touches the same objects.

Per the methodology in `CLAUDE.md`, steps 1–5 are new territory (first safe FFI layer, first generational IDs), so they're written by hand. The `gram` changes in *Build* (`crate-type`, copying the library) are plumbing and can be delegated; the schema code generator (3b) is the user's call.

## Precedents

- **Godot + godot-rust (gdext):** C++ engine, Rust gameplay as a dynamically loaded extension, `Gd<T>` smart pointer, refcounted vs manually managed objects, runtime borrow checks.
- **Unity:** C++ core, C# gameplay with domain reload. A `UnityEngine.Object` is a managed wrapper over a native object that can be destroyed underneath it.
- **Bevy assets:** strong and weak `Handle`s, the same distinction as `Reference` / `Handle` here.
- **Handmade Hero:** platform layer owns all memory, the game DLL is pure code and is reloaded live.
- **Unreal (Blueprint / Verse):** same split of engine in C++ and gameplay in another language, but the gameplay language runs in a VM rather than as a native library.
