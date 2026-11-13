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

## Suggested shape

- A small notion of **toolchain** next to the target in `target.rs`/`cmake.rs`: chosen by default from the host (`msvc` on Windows, `clang` on macOS, the system compiler on Linux), overridable with something like `gram build --target windows --toolchain mingw`.
- One place that maps (target, toolchain) to: CMake generator and arguments, vcpkg triplet, cargo triple and linker environment. Today these are spread across the `"windows"` arm and `target.rs`.
- Tests: the command-level tests run the real binary in a sandbox and need cmake and a C++ compiler; the MSVC path can only be covered on a Windows machine, so keep the mapping testable as plain data (the chosen arguments for a given (host, target, toolchain)).

## Suggested order

1. On a native Windows machine, try `gram build` as it is now (the untested first branch) and write down what breaks.
2. Introduce the toolchain notion with the current behaviour as defaults, without changing results.
3. Add MSVC (generator, triplet, triple, subsystem) behind it.
4. Re-check Linux on a real machine.
