# Notes — native toolchains per platform (Task 0.8)

Status as of 2026-11-13: **notes for future work, nothing implemented.** Written so the differences found while building on three platforms aren't rediscovered. Statements about MSVC and vcpkg triplets come from general knowledge, not from a build on native Windows — verify them when the task starts.

## Where things stand

| Host | Target | What `gram` does today |
|---|---|---|
| Linux (incl. WSL2) | linux | the system's default compiler (GCC), nothing set explicitly |
| macOS | macos | the system's default compiler (Apple clang, through CMake) |
| Linux / macOS | windows | **cross-compile with mingw**: `CMAKE_SYSTEM_NAME=Windows`, `x86_64-w64-mingw32-gcc/g++`, `-static-libgcc -static-libstdc++ -static`, vcpkg triplet `x64-mingw-static`; cargo uses `x86_64-pc-windows-gnu` with the mingw linker (`.cargo/config.toml`) |
| Windows (native) | windows | *untested.* The cmake backend's first branch (`target == host OS`) sets no compiler, so CMake picks its own default; cargo uses the host triple (MSVC). vcpkg would default to `x64-windows` |

So the mingw choice is not hard-wired for Windows in general, only for **cross**-compiling. The code is in `gram/gram-common/src/lang/cmake.rs` (the `"windows"` arm of the configure step), `gram/gram-common/src/target.rs` (`cargo_cross_triple`) and `.cargo/config.toml`.

The intended end state: native toolchains on real machines (MSVC on Windows, Apple clang on macOS, GCC or clang on a real Linux), mingw kept as the way to produce a Windows build from a Linux or macOS machine (including from WSL2, which is what is used for Windows builds on the Linux side today).

## Differences already met (what a compiler change costs)

Both of these came up as Linux/mingw GCC builds for the first time, and neither showed on macOS (clang):

- `stb_image.h`'s SSE2 resampler fails on GCC (13 in mingw-w64, 15 on Linux): `_mm_slli_si128` reports "the last argument must be an 8-bit immediate" once inlined. `VulkanImage.cpp` defines `STBI_NO_SIMD` for non-clang GNU compilers.
- GCC rejects a member named like its class (`RayTracePass& RayTracePass;`, `-Wchanges-meaning`); clang accepts it. Renamed to `m_RayPass`.

Expect MSVC to bring its own set (stricter or different warnings, `/W4`, extension differences, `std::format`/`<print>` availability by version, `__attribute__`/`#pragma` uses, designated-initializer rules).

## What changes per toolchain

- **CMake generator**: with MSVC, either a Visual Studio generator (multi-config: the build directory layout and the `--config` flag change) or Ninja from a Developer Command Prompt environment. gram currently assumes a single-config generator and copies `<build dir>/<name>`; a multi-config generator puts the binary under `<build dir>/Debug/`.
- **vcpkg triplet**: `x64-mingw-static` today. Native MSVC: `x64-windows` (DLLs, the default) or `x64-windows-static` (static CRT, closest to today's standalone `.exe`). This affects whether the executable runs without extra DLLs and whether the CRT is `/MT` or `/MD` — which must match across the engine, its dependencies and `GameLogic`'s loader.
- **Rust**: `x86_64-pc-windows-msvc` on a native Windows host; `x86_64-pc-windows-gnu` stays the cross target. The `cdylib` (`GameLogic`) and the C++ host load each other through plain `extern "C"` structs, so the ABI mixing risk is low, but both should come from the same environment.
- **Executable subsystem**: today mingw links a console application (no `-mwindows`), so a console window opens next to the game and is useful for logs. MSVC chooses at link time (`/SUBSYSTEM:CONSOLE` or `WINDOWS`); `WinMain` as the entry point needs `/SUBSYSTEM:WINDOWS` or an explicit `/ENTRY`. Decide on purpose.
- **Linker flags and runtime**: the `-static-libgcc -static-libstdc++ -static` set is mingw-only and must not leak into other branches.
- **Debug info**: MSVC produces `.pdb` files next to the binary; the cargo backend already filters `.pdb` and `.dll.a` side files when picking the artifact, the cmake side should be checked.
- **Shaders**: unchanged. `dxc` from the Vulkan SDK is host-independent and SPIR-V is portable.
- **Vulkan SDK**: on native Windows the SDK provides the loader and validation layers; the vcpkg `vulkan-loader` is used on Windows today as well.

## A real Linux (not WSL2)

- Same GCC path as now; what changes is the driver: a real GPU instead of `llvmpipe`, so the ray tracer renders at an interactive speed.
- vcpkg's loader is built with `xcb` and `xlib` (see `GramEngine/vcpkg.json`); check that it finds the GPU's ICD through the system's `icd.d` files.
- The system packages in the README (`libgl-dev`, X11 `-dev` packages, …) were verified only on Ubuntu under WSL2.

## As built (phase 2, 2026-11-15)

`gram-common/src/toolchain.rs` is the one place that knows about toolchains:

- `Toolchain { Native, Mingw, Msvc }`, parsed from `--toolchain native|mingw|msvc`.
- `resolve(host, target, requested, installed) -> Resolution { toolchain, fell_back_from }`. The candidates for (host, target), the preferred one first: linux→linux / macos→macos `[Native]`; linux/macos→windows `[Mingw]`; windows→windows `[Msvc, Mingw]`; anything else none (`Unsupported`: linux ↔ macos). Without `--toolchain`, the first installed candidate, with `fell_back_from` set when it isn't the first (gram prints a warning and goes on). With `--toolchain`, no fallback: not a candidate → `InvalidInput`, not installed → `NotFound`.
- `installed(toolchain)`: Native always (CMake finds the system compiler); Mingw = `x86_64-w64-mingw32-gcc` on the `PATH` (`gcc` on Windows); Msvc = `cl` on the `PATH` (a Developer Console) — vswhere comes with phase 4.
- `settings(host, target, toolchain) -> ToolchainSettings { generator, cmake_args, vcpkg_triplet, cargo_triple }`, read by the cmake and cargo backends. Native and Msvc set nothing (CMake/cargo pick the host's compiler, as before); mingw cross-compiling sets `CMAKE_SYSTEM_NAME=Windows`, the `x86_64-w64-mingw32-*` compilers, the static runtime, `x64-mingw-static` and `x86_64-pc-windows-gnu`; mingw on Windows (the fallback) the same without `CMAKE_SYSTEM_NAME`, with `gcc`/`g++` and the `MinGW Makefiles` generator.

`--toolchain` can't be combined with `--ci` (which builds every target with its default). The existing command tests passed unchanged, which is the proof that the defaults are today's behaviour.

## Phase 4 — on a Windows machine

What isn't done yet, and can only be tested there:

1. **Msvc settings** (`settings`, the `Msvc` arm): a Visual Studio generator (multi-config: the artifact lands in `<build dir>/<Config>/`, which `artifact_candidates` already looks into) or Ninja, `x64-windows-static`, `x86_64-pc-windows-msvc`, a consistent `/MT` CRT, the subsystem decided on purpose (see above). Add the `env` field to `ToolchainSettings` then, if gram has to load `vcvars` itself.
2. **Detection with vswhere** (`installed(Msvc)`): `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`, so a plain PowerShell finds Visual Studio, not only a Developer Console.
3. **The mingw fallback on Windows**: `.cargo/config.toml` sets the `x86_64-pc-windows-gnu` linker to `x86_64-w64-mingw32-gcc`, which a native mingw (MSYS2, w64devkit) calls `gcc`. Override it there (`CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=gcc`, through the `env` field) and check `mingw32-make` is on the `PATH` for `MinGW Makefiles`.

Test matrix:

| Where | Command | Expected |
|---|---|---|
| WSL2 (a Linux host) | `gram build --target windows` | mingw, as today: `Game.exe` runs on Windows |
| WSL2 | `gram build --toolchain msvc --target windows` | error: msvc can't build windows from a linux host |
| Developer Console | `gram build` | msvc, no warning |
| PowerShell, Visual Studio installed | `gram build` | msvc (needs vswhere, step 2) |
| PowerShell, only mingw | `gram build` | warning, then mingw (step 3) |
| PowerShell, only mingw | `gram build --toolchain msvc` | error: msvc isn't installed, no fallback |

Then re-check Linux on a real machine (the section above).
