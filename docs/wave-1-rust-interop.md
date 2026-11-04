# Wave 1 — Rust interop, as built

Status 2026-11-04. This is a record of what exists, not a plan. The design and its reasoning are in `wave-1-rust-gameplay-design.md` (state in C++, logic in Rust) and `wave-1-systems-design.md` (a generic `GameAPI`). Where those documents differ from the code, the code and this document are right. It is also the raw material for the Wave 1 devlog. *Along the way* lists the mistakes that taught something.

## The idea

**C++ owns all the state, and the reloadable Rust library holds only logic.**

- C++ owns `main()`, the window, the renderer, the entities and their components. None of it is ever reloaded.
- Gameplay code is one Rust `cdylib` (`GameLogic`, crate `game-logic` in `Game/Content/`). It keeps no game data between calls. On every call it receives a context and a table of host functions, and it reaches components through IDs.
- Recompiling the Rust code replaces it while the game runs, and the data stays where it was.

This is the "state in the host, code in the DLL" pattern from Handmade Hero, applied across a language boundary.

The earlier plan, a `bevy_ecs` World in a static Rust crate with systems in a reloaded `cdylib`, was dropped. A reload breaks whatever surviving memory still points into the unloaded library: drop glue, vtables, data laid out by an older compile, `TypeId`s. With all the data in C++ and only `#[repr(C)]` types crossing the boundary, no Rust type outlives a reload.

```
GramEngine (C++, static lib)   renderer, World, the loader, the editor panels
Game/Runtime (C++ exe)         main(), creates the World, the loader, the editor
Game/Content (Rust cdylib)     the game's systems; components generated from .ron schemas
gram-core (Rust rlib)          the Rust side of the contract, linked into the cdylib
```

## The contract: two tables, one symbol

The library exports a single symbol, `game_get_api() -> *const GameAPI`. Everything else is passed through two `#[repr(C)]` tables of function pointers. Nothing else is looked up by name.

```cpp
struct HostAPI {                                    // filled by C++: what the engine offers
    void* Host;                                     // opaque: the loader's `this`, handed back by Rust
    void  (*Log)(LogLevel, const char*);
    void  (*RegisterSystem)(void* host, const System::Descriptor*);
    void* (*GetComponent)(void* host, ObjectID, ComponentID);
};
```

```rust
#[repr(C)]
pub struct GameAPI {                                // filled by Rust: what the game offers
    pub version: u32,                               // GAME_API_VERSION = 3, checked before on_load
    pub on_load: extern "C" fn(*const HostAPI) -> *const GameAPI,
    pub on_unload: extern "C" fn(),
    pub run_system: extern "C" fn(index: u32, ctx: *mut Context),
}
```

Both tables are written by hand twice, once in C++ and once in Rust (`gram-core/src/interop/`). Their sizes are checked on both sides: `static_assert` in C++, `const _: () = assert!(size_of::<T>() == N)` in Rust.

- **The field order is the binary contract.** Only fixed-size types cross the boundary: `u32`, `u64`, `f32`, `f64`, pointers, and fixed `char[N]` buffers. The layout is therefore the same on every platform.
- **`Host` is the usual C-callback context pointer.** C++ hands Rust its own `this`, and Rust passes it back on every call. The host functions stay plain function pointers (C++ captureless lambdas), with no globals on the C++ side.

### Where the host table lives in Rust

`gram-core` keeps `static HOST: OnceLock<HostAPI>`, filled by `on_load` → `init`.

This only works because `gram-core` is an `rlib`. An `rlib` is copied into every library that links it, so each loaded copy of `GameLogic` has its own `HOST`. A reload starts with empty statics and receives the table again. The table holds host function pointers, not game data, so it doesn't break the "no state in Rust" rule.

## Loading and reloading

`GameAPILoader` (`GramEngine/Scripting/GameAPI.{hpp,cpp}`) is an application layer, and it loads the library in five steps:

1. Copy `<exe dir>/libGameLogic.dylib` to `<exe dir>/hot/libGameLogic_<N>.dylib`.
2. Open the copy with `dlopen` / `LoadLibraryW`, through `DynamicLibrary`.
3. Call `game_get_api`.
4. Check `Version`.
5. Call `OnLoad(&host)`.

Any failure leaves the game running without Rust logic, never stopped.

- **Copy-on-load.** The original file is never loaded. On Windows a loaded DLL is locked, so the build couldn't replace it. On macOS, overwriting a mapped library can crash the process.
- **A new name for every load.** macOS often doesn't really unload a Rust dylib on `dlclose`, and `dlopen` on a path that is already in memory returns the old image, with its old statics and `HOST` already set.
- **Automatic reload.** Every 0.5 s the loader compares the original file's write time with the one it last loaded. When they differ, it runs `OnUnload`, clears the system registry, unloads, copies and loads again.
- **Atomic artifacts.** `gram build` writes every artifact as `<name>.partial` and renames it over the old file. The watcher never sees a half-written library, and macOS doesn't SIGKILL a running `Game` whose file was rewritten in place.

### Panics never cross the boundary

A panic that unwinds into C++ is undefined behavior, so every exported function wraps its whole body in `guard(f) = catch_unwind(f).ok()`.

The panic hook that `init` installs logs `file:line:col: message` through `HostAPI::Log`, so the message lands in the game's log rather than on stderr. The hook itself must never panic, because a panic inside the hook aborts the process.

The result: a system that panics logs a line and is skipped for that frame, and the game keeps running.

## Systems

A system is an ordinary Rust function. The game lists its systems in one place, `Game/Content/scripts/mod.rs`:

```rust
use crate::prelude::*;

fn tick(time: Time, delta_time: DeltaTime) {
    let (now, before) = (*time, *time - *delta_time as f64);
    if now.floor() != before.floor() && before >= 0.0 {
        info!("tick: {}s", now as u64);
    }
}

export_game! {
    events: { "greet" => [greeter::greet1, greeter::greet2] },
    update: [tick],
}
```

**What `export_game!` generates.** It is a `macro_rules!` that expands, inside a private `mod __gram_export`, into:
- the `SYSTEMS: &[Entry]` list (stage, name, event, `run: fn(&Context)`);
- `on_load`, `on_unload` and `run_system`, each wrapped in `guard`;
- the `static GAME: GameAPI`;
- `game_get_api`.

Every path inside it is written through `$crate::`, so the macro doesn't depend on what the game imported. The `events` block repeats three levels deep: an optional block, then one entry per event, then one per handler.

**Registration.** `on_load` sends one `Descriptor` per entry through `HostAPI::RegisterSystem`. A descriptor holds a name, a stage (`Update`/`Event`), an event name and an index. C++ keeps a copy of each in its registry and clears the registry before every unload.

**The frame loop.** `GameAPILoader::OnDrawFrame` fills a `System::Context` (`DeltaTime`, `Time`, `EventTarget`) and runs the systems in two passes:
1. every `Update` system, through `RunSystem(index, ctx)`;
2. every queued event (`Emit(name, target)` from C++, e.g. the Greet button), given to the systems registered for it.

The queue is emptied every frame: it is taken with `std::exchange` at the start, so an event emitted while dispatching lands in the next frame.

**Adding a system** means writing a function and adding its name to `export_game!`. There's no C++ change and no version bump, and a reload picks it up.

## World, entities, components

`World` (`GramEngine/Scripting/ECS/World.hpp`) is a C++ application layer, registered before the loader.

- **Entities are generational IDs.** An `ObjectID` is an index plus a generation. The generation of a slot goes up when its entity is destroyed, and free slots are reused through a free list. A stale ID is therefore detected (`IsAlive`) and never aliases a newer entity.
- **Components are stored per type.** Each type has one `ComponentStorage` holding packed bytes, plus `Owners` (slot → entity) and `SlotOf` (entity → slot). Destroying an entity removes its components with a swap-remove.
- **Component addresses aren't stable.** They move when the storage grows, which is why Rust only ever holds one for the duration of a call.
- **A `ComponentID` is the 64-bit FNV-1a hash of the component's name.** It's computed by a `constexpr` function in C++ and a `const fn` in Rust, and a `const` assert checks that both give the same value for `"Greeting"`. Neither side needs a registration step to agree on the ID.

## Components from schemas

A component is described once, in a `.ron` file next to the scripts:

```ron
Component(
    name: "Greeting",
    fields: [
        Field(name: "text", ty: Str(64)),
        Field(name: "scale", ty: F32),
    ],
)
```

`gram build` runs the generator (`gram-common/src/schema/`, also available as `gram generate`) over the whole tree before compiling anything. It writes `components.generated.rs` and `Components.generated.hpp` next to the `Cargo.toml`, and only rewrites a file when its content changed. Both files are git-ignored. The generator produces:

- **Rust:** a `#[repr(C)]` struct, `impl Component { const ID }`, and `const` asserts on the size and on every offset. It imports only `use gram_core::codegen::*;`: a list that `gram-core` keeps for generated code alone, separate from the scripts' prelude.
- **C++:** the same struct with PascalCase field names, `static_assert`s on the size and the offsets, and an `inline` `ComponentDescriptor`. The descriptor holds the name, the ID, and one `FieldDescriptor` per field (name, type, offset, size).

The generator computes the layout itself, using C's rules:
- each field's offset is rounded up to that field's alignment;
- the struct's size is rounded up to the largest alignment, which starts at 1.

Both compilers then confirm the result, so a disagreement fails at compile time and never at runtime.

The ImGui inspector draws any component from its descriptor, with one widget per `FieldType`. A new field therefore shows up in the editor without any C++ change, as long as its type has a widget there.

Changing a schema means a rebuild and a restart, because a reload replaces code, never the shape of data already in memory. Every hot-reload system has this limit.

## Reaching components safely

The goal is that game code contains no `unsafe`, no raw pointers and no `extern "C"`, and still can't create two `&mut` to the same component.

### `Query<T>`

```rust
pub(crate) fn greet1(target: EventTarget, greetings: Query<Greeting>) {
    let Some(entity) = *target else { return };

    if let Some(greeting) = greetings.get(entity) {
        info!("Greetings from Rust: {}!", greeting.text.as_str());
    }
    if let Some(mut greeting) = greetings.get_mut(entity) {
        greeting.text.set("Hello world from Rust");
    }
}
```

`Query<'a, T>` is a view over the frame's `Context`, with two methods:
- `get(id)` returns `Option<Ref<'a, T>>`;
- `get_mut(id)` returns `Option<RefMut<'a, T>>`.

Both ask C++ for the address through `GetComponent(entity, T::ID)`, and both return `None` for a dead entity or a missing component.

The guards follow the `RefCell` model. Each one holds the reference plus a `BorrowGuard`, and implements `Deref` (plus `DerefMut` for `RefMut`), so `greeting.text` works on the guard directly. Their lifetime `'a` comes from the `Context`, so the compiler rejects any guard that would outlive the system call.

`get_mut` takes `&self`, not `&mut self`. That lets you hold two `get_mut` on *different* entities at once, which is how you swap a value between two entities. The check moves to runtime, and covers the same entity only.

The original closure API is still there underneath: `ctx.with::<T>(id, |c| …)` and `with_mut`.

### `BorrowGuard`: RefCell rules across FFI

The borrow checker can't see that two IDs name the same C++ memory, so the check happens at runtime.

- A `thread_local!` list holds the active borrows, as entity + component + mutable.
- `acquire` panics on a conflict. Two reads may overlap, but a write conflicts with anything on the same entity and component. Different entities or components never conflict.
- The guard is taken **before** the reference is created, so two references to the same memory never exist at the same time.
- The guard releases itself in `Drop`, so it is released during unwinding as well. A system that panics doesn't leave its borrow behind for the next frame.
- `#[track_caller]` makes the panic report the line in the game's code rather than the line inside `gram-core`.

### System parameters

A system declares what it needs in its signature, and C++ still calls one signature, `fn(&Context)`. Two traits connect the two:

```rust
pub trait SystemParam {
    type Item<'a>;
    fn fetch<'a>(ctx: &'a Context) -> Self::Item<'a>;
}

pub trait IntoSystem<Marker> {
    fn run(&self, ctx: &Context);
}

// one impl per arity, generated by a macro_rules! for 1 to 12 parameters:
impl<Func, A: SystemParam, B: SystemParam> IntoSystem<(A, B,)> for Func
where Func: Fn(A, B) + for<'a> Fn(A::Item<'a>, B::Item<'a>)
{
    fn run(&self, ctx: &Context) { self(A::fetch(ctx), B::fetch(ctx)) }
}
```

`export_game!` turns each listed system into `run: |ctx| IntoSystem::run(&greet1, ctx)`. A closure that captures nothing coerces to `fn(&Context)`, even inside a `const`. The `SYSTEMS` table therefore keeps exactly the shape C++ expects.

Three pieces needed explaining:

- **`type Item<'a>`** (a generic associated type, GAT) exists because `Self` is a fixed type, with one specific lifetime. `fetch` has to produce `Query<'a, T>` for the lifetime of *this* frame's `Context`, and `Item<'a>` means "the same type, with the lifetime I give you now".
- **The double bound.**
  - `Fn(A, B)` lets the compiler *infer* the parameter types from the function's signature.
  - The higher-ranked `for<'a> Fn(A::Item<'a>, …)` is what makes the call valid for any lifetime.
  - With only the first bound the call doesn't compile, and with only the second nothing can be inferred.
  - Bevy uses the same trick.
- **The marker `(A, B,)`** gives each arity a different trait type. Without it, the `Fn(A)` and `Fn(A, B)` impls would be rejected as possibly overlapping. Note the trailing comma inside the repetition: it makes a one-parameter marker the tuple `(A,)`, not just `A` in parentheses.

**The parameters available today:**
- `&Context`, so plain `fn(&Context)` systems still work;
- `Query<T>`;
- `EventTarget(Option<ObjectID>)`;
- `Time(f64)`;
- `DeltaTime(f32)`.

The last three are generated by a crate-private `system_param!` macro (struct, `SystemParam`, `Deref`). C++ passes `InvalidObjectID` as the target of `Update` systems, so `EventTarget` is `None` there.

### What a script imports

There are two preludes, each read by one reader:

- **`gram_core::prelude`**, for what a human writes in a system: `Context`, `Query`, `EventTarget`, `Time`, `DeltaTime`, `Str`, `ObjectID`, the log macros and `export_game!`. It lists them by name, with no globs, so a new `pub` item in `gram-core` doesn't reach game code by accident.
- **`gram_core::codegen`** (`#[doc(hidden)]`), for what the generator emits: `Component`, `ComponentID`, `component_id`, `Str`. A new schema field type, such as `Vec3`, adds its Rust type here, next to its `FieldType` case.

The game's own prelude (`Game/Content/lib.rs`) is `gram_core::prelude` plus the generated components, so a script starts with `use crate::prelude::*;`.

**`gram-core`'s layout:**
- `interop/`, the FFI plumbing: `host` (`HostAPI`, `init`, `host()`, `guard`), `logging`, and `game` (`GameAPI`, `export_game!`);
- `scripting/`, what a script works with: `system` (`context`, `query`, `params`), `component`, `world`, `types`, `borrow`.

## The editor

These are ImGui panels that are the same for every game, in `GramEngine/Editor/`:

- **Outliner:** lists the live entities, each with its components.
- **Inspector:** shows the selected entity's components, drawn from their descriptors.
- **Systems:**
  - a Reload button;
  - one row per registered system, with name, event, stage and the duration of its last run;
  - an enabled checkbox per system. Disabled systems are remembered by name, so they stay disabled across reloads.

The panels are reached through a Window menu, where `Editor::AddPanel<T>` adds a game's own panels, like the Greet button. Editing the text in the inspector, rebuilding the Rust code and watching the systems table update live is the Wave 1 proof of concept working end to end.

## Along the way

Things that went wrong and what they taught. Most of them are worth a paragraph in the devlog.

- **A field missing from a C++ designated initializer is silently zeroed.**
  - The first time, a null function pointer in `HostAPI` reached Rust and aborted the load. Since then the panic hook falls back to stderr until the host table is stored.
  - The second time, the frame's `Context` left out `EventTarget`. `Update` systems then saw `{0, 0}`, which is a live entity, as their target. It now gets `InvalidObjectID` explicitly. A `Debug` print from a `Query` made the bug visible.
- **Layout arithmetic.** The first version of the generator used each field's size as the next offset (`[1, 4]` instead of `[0, 4]`). The struct's alignment started at 0 instead of 1, which panicked on an empty component; a unit test caught it. Once the C++ and Rust asserts are generated, both compilers check the generator.
- **`#[track_caller]` stops at closures.** With the `panic!` inside `BORROWS.with(|list| …)`, the reported location was `borrow.rs`, not the game's line. The fix: compute the conflict inside the closure and panic outside it.
- **`Drop` must find its own borrow, not a conflicting one.** The first `Drop` searched with the conflict condition, so it could remove a different borrow. Equality is the right test.
- **`let _ = guard` drops immediately; `let _borrow = guard` keeps it alive.** One character decides whether the protection exists.
- **An ImGui layer that returns `false` skips every layer after it.** A panel returned its "open" flag, so closing it removed the panels drawn after it. Panels now always return `true`.
- **Macros must not lean on the caller's imports.** `export_game!` wrote `IntoSystem::run` without `$crate::`. It compiled only because the prelude used a glob that happened to bring `IntoSystem` in, so making the prelude explicit would have broken every game. For the same reason, generated code got its own import list (`codegen`) instead of `use crate::prelude::*`, which had made the generated file and the game's prelude import each other.
- **`macro_rules!` hygiene.** An identifier written inside a macro and one written by the caller are different even when they're spelled the same. `system_param!` takes the fetch expression as a real closure (`|ctx: &Context| ctx.time()`) and calls it, rather than referring to a `ctx` defined inside the macro.
- **`#[derive(Debug)]` on a generic struct requires `T: Debug`.** It applies even when `T` sits only in a `PhantomData`. Printing a `Query<Greeting>` meant deriving `Debug` on the generated components too.
- **A compiler update invalidates precompiled headers**, and `cmake --fresh` doesn't remove them. The error is "PCH file … built from a different branch", and the fix is to delete the build directory.
- **Writing over a running executable on macOS** gets the next launch SIGKILLed, because the code signature no longer matches. Hence `.partial` + rename.

## Tests

- **`gram-core`: 23 unit tests.**
  - 9 cover `BorrowGuard`: the RefCell rules, release on drop and on unwind, and the panic location.
  - 14 cover `Query`, the parameters and `IntoSystem`. They run against a fake host whose `get_component` looks components up in a table each test fills.
- **`gram-cli/tests/generate.rs`: 7 command-level tests** of the generator:
  - only `Component(` schemas are picked up;
  - C layout;
  - an unchanged schema rewrites nothing;
  - a broken schema names its file;
  - a non-cargo module is rejected;
  - `gram build` generates the files.

## Not done yet

- **Iterating a component.** `Query::iter` / `iter_mut` (every entity that has a `T`) needs a new `HostAPI` function and `GameAPIVersion` 4. Wave 2 needs it, when Rust moves the spheres.
- **Typed access from C++.** `world.Get<Greeting>(e)` and `world.Each<Sphere>(…)` need a `static constexpr ComponentID ID` in each generated struct. Today C++ reaches components through `void*` + `ComponentID`.
- **Other features:**
  - events emitted by Rust;
  - system ordering inside a stage;
  - `Load` / `Startup` stages;
  - more schema field types (`Vec3` first);
  - separate roles for the two languages and a prototyping mechanism (both noted, not needed now).
