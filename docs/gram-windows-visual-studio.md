# Report — Windows, MSVC and Visual Studio (phase 4 of Tasks 0.8 / 0.9, 2026-11-16)

Phases 1–3 of the configurations/toolchains/IDE plan (`docs/salutare-p-n-acum-am-unified-mountain.md`) were done on macOS; this is phase 4, done on a Windows 11 machine with Visual Studio 2026 Community (MSVC 14.52, its bundled CMake 4.3.1), the Vulkan SDK 1.4.363, vcpkg in `C:\vcpkg` and Rust's `x86_64-pc-windows-msvc`. It follows the handoff in `docs/gram-native-toolchains.md` (*Phase 4*) and adds what came up on the way: a Play / Pause / Stop toolbar, because Visual Studio can't build while it debugs.

Who wrote what: the user wrote the `Msvc` settings, the `World` snapshot and the loader's play state (`ReplayState`); Claude wrote the plumbing (vswhere, `gram ide vs`'s fixes, `.gram-build`, `RunProcess`, the toolbar, the Final subsystem, tests) and reviewed.

## Step 0 — the tree as it was, under MSVC

`gram build` from a Developer PowerShell built the whole tree with MSVC on the first try: no C++ change. The differences GCC brought (stb's SSE2 resampler, a member named like its class) had already been fixed, and MSVC found nothing new. The game ran: Vulkan, the shaders, `GameLogic.dll` loaded, its systems running.

Two problems showed up before the build itself:

- **`VCPKG_ROOT` inside a Developer PowerShell** points at the vcpkg bundled with Visual Studio (`...\Community\VC\vcpkg`), which only works in manifest mode with a `builtin-baseline`. Fixed with a baseline in `GramEngine/vcpkg.json` (the commit of the standalone vcpkg the tree was built with, so the versions don't move); `Runtime` gets it through the manifest merge. Both vcpkg copies work now. Side effect: another machine's vcpkg checkout must contain that commit (`git pull` in `$VCPKG_ROOT`).
- **No `cmake`, no `cl` on the PATH** in a plain PowerShell: only the Developer PowerShell adds them (step 1).

## Step 1 — finding Visual Studio (vswhere)

`toolchain.rs`:

- `visual_studio()` asks `vswhere.exe` (always at `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\`) once for the newest installation with the C++ tools.
- `installed(Msvc)` = `cl` on the PATH **or** vswhere finds one: the Visual Studio generator finds the compiler on its own, without the Developer PowerShell's environment.
- `cmake()` is the one place gram starts cmake from: the one on the PATH, else the one Visual Studio ships (`Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`), else an error that says what's missing (it used to be the OS's "program not found"). The cmake backend and `gram ide` use it.

So `gram build` and `gram ide vs` work from any terminal.

## Step 2 — msvc's settings (user)

`toolchain::settings("windows", "windows", Msvc)`:

| Setting | Value | Why |
|---|---|---|
| generator | `Visual Studio 18 2026` | explicit, so the build doesn't depend on what CMake finds first |
| cmake args | `-A x64`, `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>` | the static CRT (`/MT`, `/MTd` in Debug) for everything CMake compiles |
| vcpkg triplet | `x64-windows-static` | the dependencies built with the same CRT, as static libraries (mixing `/MT` and `/MD` fails with `LNK2038`) |
| cargo triple | none | the host's `x86_64-pc-windows-msvc` |

The result is like mingw's: a `Game.exe` without DLLs next to it, except `vulkan-1.dll` (vcpkg builds the Vulkan loader only as a DLL) and `GameLogic.dll` (the game's code, which keeps its own CRT: only `extern "C"` structs cross between them, no memory allocated on one side and freed on the other). `gram ide vs` configures with the same settings.

## Step 3 — the configurations under MSVC

`GramConfigurationFlags.cmake` derives the flags from the compiler's own, so it worked unchanged:

- Release = RelWithDebInfo's: `/Zi /O2 /Ob1 /DNDEBUG`.
- Final = Release's: `/O2 /Ob2 /DNDEBUG`, with LTO turned into `<WholeProgramOptimization>true</WholeProgramOptimization>` in the `.vcxproj` (`/GL` + `/LTCG`).

Three command tests assumed GCC/clang (`-O2 -g`, `-flto` in `compile_commands.json` — which the Visual Studio generator doesn't write — and `libmain.a`); they now check MSVC's values on Windows. One test found a real bug: gram's scaffold of a cmake `SHARED` library exported nothing under MSVC (no `__declspec(dllexport)`), so MSVC wrote no import library and the executable couldn't link. The scaffold adds `WINDOWS_EXPORT_ALL_SYMBOLS ON`. `cargo test` passes in full on Windows.

## Step 4 — no console in Final

A Windows executable carries a *subsystem* in its header: CONSOLE (Windows opens a console before `main`: the logs, useful while developing) or WINDOWS (no console: what a shipped game wants). The linker picks it. `GramEngine/CMakeLists.txt` adds, as `INTERFACE` link options (so they land on the game's executable, whatever its name), only for Final on Windows:

- MSVC: `/SUBSYSTEM:WINDOWS` plus `/ENTRY:mainCRTStartup` — the windows subsystem would otherwise look for `WinMain`; this keeps `main` as the entry point, no C++ change;
- mingw: `-mwindows`.

Checked in the executables' PE headers: Final has subsystem 2 (windows), Debug and Release 3 (console).

## Step 6 — `gram ide vs`

`gram ide vs [--open]` writes the superproject into `build/ide/vs/` and configures it with the Visual Studio generator. What had to change from the Xcode version:

- **The PATH baked into the `GameLogic` target** broke the generated CMake: backslashes read as escapes (`Invalid character escape '\P'`), and `;` would have split it into a list. It's escaped now: `\\` and `$<SEMICOLON>`.
- **The solution is `gram-engine.slnx`**: CMake 4.3 writes the XML solution for Visual Studio 2026. gram opens whichever of `.slnx` / `.sln` exists.
- **`VS_STARTUP_PROJECT`** = the game's runtime module, so F5 starts `Game`, not `ALL_BUILD`.
- **The `.hlsl` files** added to `GameLogic` only to be shown were compiled by Visual Studio's own HLSL compiler (FXC), which fails on the Vulkan attributes; they're marked `HEADER_FILE_ONLY` and `VS_TOOL_OVERRIDE "None"` (FXC ignores the first).

Verified in Visual Studio: the solution opens with GramEngine / GramRenderer / GramScripting / Runtime / GameLogic / gram-core, F5 starts the game, breakpoints hit. The debugger's working directory doesn't matter (the game finds its files next to the executable), and the `.pdb` lands next to `Game.exe`.

## Hot reload under Visual Studio: Play / Pause / Stop

Xcode builds a target while the game runs under its debugger; Visual Studio doesn't (it asks to stop debugging, and refusing skips the build). So the build moved into the game: the editor got a toolbar with the three buttons of a game editor.

```
            Play                   (gram done)
  Stopped ────────▶ Building ─────────────────▶ Playing ◀──┐
     ▲                 │  (build failed:            │  │    │ Play: rebuild
     │                 │   back where it came from) │  └────┘ without stopping
     │                 ▼                            │ Pause
     └──── Stop ─── Paused ◀────────────────────────┘
                       │ Play ──▶ Building ──▶ Playing
```

- **gram** writes `<module>.gram-build` next to a `dynamic` module it builds for this machine (not with `--ci`): one `<key> <value>` per line — `dir` (the project root), `env PATH=...` (a game started from an IDE or the Finder doesn't get the shell's), `arg` (gram's path, then `build --module GameLogic --config <config>`). Written only when its content changes.
- **`Platform.hpp`**: `ReadProcessCommand(path)` reads that file; `RunProcess(command, onLine)` runs it to its end and hands every line of output (stdout and stderr together) to a callback, returning the exit code (-1 when it couldn't start). `CreateProcessW` with a pipe and no console window on Windows, `posix_spawn` elsewhere.
- **`World`** (user): `TakeSnapshot` / `RestoreSnapshot` — a copy of the slots, the free list and the component storages. Plain copies are enough: components hold fixed-size fields only, and Rust only gets addresses for the duration of a call.
- **`GameAPILoader`** (user, the state machine): `ReplayState { Stopped, Building, Playing, Paused }`, changed only through `Play()`, `Pause()`, `Stop()` (events) and `FinishBuild()` (the loader's own), each starting with a guard on the current state.
  - Play from Stopped or Paused starts `RunProcess` on a worker thread (`std::async`); each frame asks the `std::future` whether it's done (`wait_for(0s)`), never waiting. Success: reload if the library's write time changed, then Playing (from Stopped: take the snapshot, reset the time). Failure: an error in the Log, back to where it came from.
  - Play while Playing rebuilds without stopping: the systems keep running during the build (Building coming from Playing), and the new library is loaded at the end.
  - Pause holds the systems; Stop restores the snapshot and empties the event queue.
  - Systems run only while playing; `Context::Time` adds up `DeltaTime` while playing, so it doesn't jump after a pause.
  - The build's output goes to the Log panel through the editor's logger, taken on the main thread (spdlog's `_mt` sinks are thread-safe; `GetSystemLogger` creates loggers lazily and isn't).
  - Debug (the editor) starts Stopped; Release and Final have no toolbar and play from the start.
  - The 0.5 s watcher stays, so a build from a terminal still reloads.
- **`EditorToolbar`**: a strip under the main menu bar (`BeginViewportSideBar`, which shrinks the dockspace like the menu bar does), registered right after the menu bar, Debug only. Three icon buttons drawn with ImGui's draw list (the default font has no ▶), centered, enabled by state, the current one highlighted, tooltips; the state as text next to them.

Verified in Visual Studio with the debugger attached: Play builds and starts the systems; Pause, a change in `Game/Content/scripts`, Play rebuilds and continues from the same time; Stop puts the `Greeting` text edited during Play back; a Rust compile error leaves the game where it was, with the error in the Log; Play while playing rebuilds without stopping.

## Left

- **On macOS:** compile the POSIX `RunProcess` (written, not compiled yet) and try Play / Pause / Stop and breakpoints in Xcode; `git pull` in `$VCPKG_ROOT` if vcpkg doesn't know the baseline. The same on a real Linux, plus the checks in `docs/gram-native-toolchains.md` (*A real Linux*).
- **The mingw fallback on Windows** (step 5): untested, no mingw on this machine. When it's tried: [WinLibs](https://winlibs.com)' zip (GCC 15 + `mingw32-make`, no installer) on the PATH of one terminal only, `rustup target add x86_64-pc-windows-gnu`, and the cargo linker named `gcc` instead of `x86_64-w64-mingw32-gcc` (`CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER`, through an `env` field in `ToolchainSettings`). Not Code::Blocks: its older bundled GCC (8.x up to 20.03) is too old for C++20 and `std::format`.
- **Packaging** (an idea for when there's a concrete game): Final without a console is a first piece; others are an engine shipped as `GramEngine.lib` plus its public headers (an editor without the engine's sources, like a proprietary engine), and a game folder with only what it needs.
