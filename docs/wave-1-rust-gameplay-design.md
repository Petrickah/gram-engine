# Wave 1 — Rust gameplay layer over a C++ engine (design proposal)

Status as of 2026-10-21: **proposal, not adopted yet.** Nothing is implemented. `CLAUDE.md` and Tasks 1.3/1.4 in `.gram.kanban.md` still describe the original two-crate plan (`gram-core` static + `gram-hot` dynamic, `bevy_ecs` World in Rust). If this design is adopted, update both (see *Impact on the plan*). Statements about third-party engines and platform behavior come from memory; re-check them before depending on them.

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
gram-hot   (Rust cdylib)       stateless Object Manager + Handle/Reference + gameplay code
```

- Matches the spirit of `CLAUDE.md` ("C++ owns `main()` and the renderer core ... Rust is the reloadable gameplay layer"). What changes: `gram-core` disappears, and the world's state moves from `bevy_ecs` to C++.
- The boundary is plain `extern "C"` plus `#[repr(C)]` structs in both directions. No `cxx`, no Corrosion static link.

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

- Every shared type is defined twice, in C++ and in Rust, with the same layout. Check it on both sides: `static_assert(sizeof(T) == N && alignof(T) == M)` in C++, `const _: () = assert!(size_of::<T>() == N);` in Rust.
- Later, generate one side from the other (`cbindgen` from Rust, or `bindgen` from C++) instead of keeping both by hand.
- **Changing a shared layout needs a full restart** (or a migration step). A reload only replaces code, never the shape of data already in memory. This is the same limit every hot-reload system has.

## Platform pitfalls

- **Windows locks a loaded DLL**, so the compiler can't overwrite it. Load a *copy* (`gram-hot-1.dll`, `gram-hot-2.dll`, …) and build over the original.
- **macOS: Rust `thread_local`s with destructors can make `dlclose` a no-op**, so the old code silently stays loaded. One more reason for rule 2. Verify on the current toolchain.
- **Panics must not unwind across `extern "C"`.** Depending on the Rust version and ABI this aborts or is undefined behavior. Wrap every exported function body in `std::panic::catch_unwind` and report failure to the host through the return value.

## Build

- A single `cargo` module of kind `library` producing a `cdylib`. `gram` doesn't manage `crate-type` yet (`compile_cargo` notes that libraries default to `rlib` and nothing is copied to `build/`), so `gram` needs: `crate-type = ["cdylib"]` support and copying the `.dll`/`.so`/`.dylib` next to the executable, like it does for shaders today.
- Corrosion is no longer needed to link Rust into C++. The C++ side only `dlopen`s/`LoadLibrary`s the result.

## Open questions

- **ECS in C++:** write a small one (component arrays + generational IDs), or use EnTT? Writing it is a real learning task; EnTT is a known quantity.
- **Ergonomics vs `bevy_ecs`:** gameplay loses typed queries and system parameters. A thin query API over `HostApi` (for example "iterate all entities with component X") may be enough for Wave 1/2.
- **Threading:** assume single-threaded gameplay for now. Multi-threaded access changes the aliasing story.
- **Is the original plan still preferable?** Yes, if writing gameplay with `bevy_ecs` in Rust is itself the goal. Then the rule is strict: every type in `gram-core`, nothing with `Drop` or a vtable defined in `gram-hot`.

## Impact on the plan (if adopted)

- **`CLAUDE.md`, Wave 1 description:** replace the `gram-core` / `gram-hot` split and the `cxx` + Corrosion mention with this architecture.
- **Task 1.3** becomes: "Rust `cdylib` + host loader: C++ loads `gram-hot` (copy-on-load), calls `on_load` / `update` / `on_unload` through `extern "C"`, panics caught at the boundary."
- **Task 1.4** becomes: "C++ owns a `Sphere` object; Rust moves it through a `Handle`; reload picks up new logic live, state untouched." The drawing notes in `wave-1-2-sphere-rendering-notes.md` are unaffected; only the sphere's owner changes from `gram-core` to C++.
- **ImGui (Task 1.y):** unaffected. The ImGui context already stays on the C++ side.

## Suggested order

1. Host loader + `extern "C"` lifecycle with an empty Rust library (copy-on-load, reload on file change, `catch_unwind`).
2. `HostApi` with `log` only: prove a call in each direction survives a reload.
3. `ObjectId` + ID table in C++, `Handle<T>` in Rust, one object (the sphere).
4. `Reference<T>` once something needs to be kept alive (assets).
5. Aliasing guard (closure access or borrow flags) before more than one gameplay system touches the same objects.

Per the methodology in `CLAUDE.md`, steps 1–5 are new territory (first safe FFI layer, first generational IDs), so they're written by hand. The `gram` changes in *Build* (`crate-type`, copying the library) are plumbing and can be delegated.

## Precedents

- **Godot + godot-rust (gdext):** C++ engine, Rust gameplay as a dynamically loaded extension, `Gd<T>` smart pointer, refcounted vs manually managed objects, runtime borrow checks.
- **Unity:** C++ core, C# gameplay with domain reload. A `UnityEngine.Object` is a managed wrapper over a native object that can be destroyed underneath it.
- **Bevy assets:** strong and weak `Handle`s, the same distinction as `Reference` / `Handle` here.
- **Handmade Hero:** platform layer owns all memory, the game DLL is pure code and is reloaded live.
- **Unreal (Blueprint / Verse):** same split of engine in C++ and gameplay in another language, but the gameplay language runs in a VM rather than as a native library.
