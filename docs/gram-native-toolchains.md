# Notes — native toolchains per platform (Task 0.8)

Status as of 2026-11-16: **implemented** (phase 2 on macOS, phase 4 on Windows: see the sections at the end); the mingw fallback on Windows is untested. Originally, 2026-11-13: notes for future work, nothing implemented. Written so the differences found while building on three platforms aren't rediscovered. Statements about MSVC and vcpkg triplets come from general knowledge, not from a build on native Windows — verify them when the task starts.

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


### A Windows device (an old laptop, 2026-11-22)

Windows 11 (build 26200), Intel Iris Xe + NVIDIA RTX 3050 Laptop, 1920x1080@144, reached from the Mac by `gram deploy` (README): nothing is built there, mingw builds it here.

- **OpenSSH Server**: `Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0`, `Start-Service sshd`, `Set-Service sshd -StartupType Automatic`; the firewall rule wasn't created (port 22 closed): `New-NetFirewallRule -Name sshd -DisplayName 'OpenSSH Server (sshd)' -Enabled True -Direction Inbound -Protocol TCP -Action Allow -LocalPort 22`. PowerShell as its shell: `HKLM:\SOFTWARE\OpenSSH` `DefaultShell`. The Mac's key (`~/.ssh/id_ed25519_laptop`, a `Host laptop` entry): for an administrator it goes in `%ProgramData%\ssh\administrators_authorized_keys`, which must have exactly `icacls … /inheritance:r /grant "Administrators:F" /grant "SYSTEM:F"`, else it's silently ignored. Plan B if the capability won't install (it goes through Windows Update): Microsoft's MSI from `PowerShell/Win32-OpenSSH`.
- **Starting it on the desktop**: a program started over ssh runs in session 0, with no desktop; a scheduled task with an Interactive principal runs in the logged-on user's (`SessionId: 1`). Its output goes through `cmd /c … > log`, which also works for Final's `-mwindows` (no console). cmd, a console program, opened its own black window behind the game; run inside `conhost.exe --headless` (a console without a window) it doesn't, and the log still fills.
- **Found**: the device scoring picked the RTX over the Iris Xe; ImGui follows a scale change at runtime (Task 1.x-a); Debug lacks the validation layers (no Vulkan SDK there: an error, then it goes on); **Smart App Control** (on by default on a fresh Windows 11) blocked the unsigned `GameLogic.dll` copy (`hot\GameLogic_1.dll`: "An Application Control policy has blocked this file", Code Integrity events 3033/3077/3118) after letting it through once: its decision comes from a cloud lookup, so for code built here it's unpredictable. Turned off on the laptop (and it can be turned on again from the same menu on this build); for players it means signing (Task P.x).
- PowerShell over ssh without a terminal prints its progress records as `#< CLIXML`: `$ProgressPreference = 'SilentlyContinue'` at the top of every script.

### SteamOS (Legion Go S, 2026-11-16)

A handheld on SteamOS 3.9 (Arch-based, x86_64, read-only system), used as "a Linux device over SSH"; Valve's SteamOS Devkit Client didn't install on macOS, `gram deploy` (Task 0.12) replaces it. What it took:

- **On the device** (Desktop Mode, Konsole): `passwd` (no password by default), `sudo systemctl enable --now sshd`. On the Mac: a key of its own (`ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_legion`, `ssh-copy-id`) and a `Host legion` entry in `~/.ssh/config` (`User deck`).
- **A distrobox** (preinstalled with podman): `distrobox create --name gram --image docker.io/library/ubuntu:24.04`, the same Ubuntu as WSL2. It shares the home, the GPU and the display; `sudo` needs no password inside. Packages: `build-essential cmake ninja-build git curl zip unzip tar pkg-config autoconf automake libtool python3 python3-venv libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libwayland-dev libxkbcommon-dev wayland-protocols libx11-xcb-dev libxcb1-dev mesa-vulkan-drivers vulkan-tools rsync xz-utils`, plus **`autoconf-archive`** (vcpkg's `pthread-stubs`, built for the xcb Vulkan loader, fails without it; WSL2 had it already) and `libltdl-dev bison flex gettext`.
- **Tools in the home**: rustup, vcpkg (`~/vcpkg`), the LunarG Vulkan SDK tarball (`~/vulkansdk/<version>`, for `dxc` and the validation layers). `~/.gram-env` sets `VCPKG_ROOT`, sources the SDK's `setup-env.sh` and cargo's env; `~/.bashrc` reads it only inside the container (`$CONTAINER_ID`), so SteamOS itself is untouched.
- **The repo**: `rsync` from the Mac (without `build/`, `target/`, `build-*/`, `.vscode/settings.json`), so no GitHub credentials on the device yet. Since 2026-11-21 `gram deploy` does it, then builds and runs (the steps below, automated): with a `Devices.ron` next to `Gram.ron` listing the device (`Device(name: "legion", host: "legion", dir: "Projects/gram-engine", container: Some("gram"), env: Some("~/.gram-env"))`), `gram/target/debug/gram deploy` from the repo root; `--dry-run` prints the commands.
- **Game Mode** (gamescope only shows what Steam starts, so a game started over ssh runs unseen): every deploy installs `~/gram-launch.sh` (and `~/.gram-launch`, the build to run); once, in Desktop Mode, *Steam → Games → Add a Non-Steam Game* → `/home/deck/gram-launch.sh`, start it from the library, and copy the `SteamAppId` it logged in `~/gram-launch.log` into the device's `steam: Some(...)` in `Devices.ron`. From then on `gram deploy` starts the game through Steam (`steam steam://rungameid/<id>` over ssh works, the id being `app id << 32 | 0x02000000`, what Steam hands the game as `SteamGameId`) and follows its log. Found 2026-11-21: Game Mode's session is `DISPLAY=:1` with no `XAUTHORITY` (Desktop Mode: `:0` and an `xauth_*` file); Steam's overlay comes in as Vulkan layers and works (the Steam button, navigating with the controller, quitting); its 32-bit `gameoverlayrenderer.so` in `LD_PRELOAD` fails harmlessly.
- **Debugging** (2026-11-22): `sudo apt install lldb` in the distrobox (lldb 18 there, Apple's lldb on the Mac: the GDB remote protocol doesn't care); `ptrace` works in the distrobox for a game `lldb-server` starts itself, no extra capability needed. `gram deploy --debug` does the rest (README). Learned on the way: an `lldb-server gdbserver` serves one connection, then exits (a port test with `nc` used it up); a gdbserver alone leaves lldb without the libraries it doesn't have locally (`libGameLogic_<n>.so`, a copy per reload), hence the platform next to it; the platform must not start the game itself, or it gets the Mac's environment instead of `~/.gram-env`. A game stopped at a breakpoint looks idle: the device dimmed, then the power button (pressed to wake it) suspended it and the connection dropped. `systemd-inhibit` over ssh is refused ("requires interactive authentication": polkit grants it to the session on the screen only), but the launcher Steam starts is in that session, so it holds `idle:sleep` while the game is debugged; tried in Game Mode: Steam still lowers the brightness (its own dimming), the device doesn't sleep (awake after minutes untouched). To light a dimmed screen: touch it or press a button, not power. Also set on the Legion Go S: Developer Mode → Wi-Fi power management off (steadier ssh).
- **Running from SSH** on the device's screen: the variables of the running session, `DISPLAY=:0 WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 XAUTHORITY=/run/user/1000/xauth_*` (Desktop Mode is Wayland; GLFW goes through XWayland).

Result: `gram build` built the whole tree; the game picks the GPU (AMD Ryzen Z2 Go, RADV) over `llvmpipe`, with no validation error. Tried on the device (user): Play rebuilds `GameLogic` in the container (cargo's lines in the Log: the POSIX `RunProcess`) and the systems tick, Greet logs, Stop restores the `World`; the book's final scene renders at about 15 FPS in Debug (the M4 Pro: about 8), every ray against every sphere until there's a BVH (Task 2.x).

## As built (phase 2, 2026-11-15)

`gram-common/src/toolchain.rs` is the one place that knows about toolchains:

- `Toolchain { Native, Mingw, Msvc }`, parsed from `--toolchain native|mingw|msvc`.
- `resolve(host, target, requested, installed) -> Resolution { toolchain, fell_back_from }`. The candidates for (host, target), the preferred one first: linux→linux / macos→macos `[Native]`; linux/macos→windows `[Mingw]`; windows→windows `[Msvc, Mingw]`; anything else none (`Unsupported`: linux ↔ macos). Without `--toolchain`, the first installed candidate, with `fell_back_from` set when it isn't the first (gram prints a warning and goes on). With `--toolchain`, no fallback: not a candidate → `InvalidInput`, not installed → `NotFound`.
- `installed(toolchain)`: Native always (CMake finds the system compiler); Mingw = `x86_64-w64-mingw32-gcc` on the `PATH` (`gcc` on Windows); Msvc = `cl` on the `PATH` (a Developer Console) — vswhere comes with phase 4.
- `settings(host, target, toolchain) -> ToolchainSettings { generator, cmake_args, vcpkg_triplet, cargo_triple }`, read by the cmake and cargo backends. Native and Msvc set nothing (CMake/cargo pick the host's compiler, as before); mingw cross-compiling sets `CMAKE_SYSTEM_NAME=Windows`, the `x86_64-w64-mingw32-*` compilers, the static runtime, `x64-mingw-static` and `x86_64-pc-windows-gnu`; mingw on Windows (the fallback) the same without `CMAKE_SYSTEM_NAME`, with `gcc`/`g++` and the `MinGW Makefiles` generator.

`--toolchain` can't be combined with `--ci` (which builds every target with its default). The existing command tests passed unchanged, which is the proof that the defaults are today's behaviour.

## Phase 4 — done 2026-11-16

Steps 0–4 and 6 below are done; what was built and found is in `docs/gram-windows-visual-studio.md`. Left: step 5 (the mingw fallback on Windows, untested: no mingw on the machine) and the re-checks on macOS and a real Linux. The handoff stays below as it was written.

## Phase 4 — on a Windows machine (handoff)

Phases 1–3 (build configurations, toolchains, `gram ide xcode`) were done on macOS on 2026-11-14/15; what's left needs a Windows machine. This section is meant to be enough on its own: read it with `CLAUDE.md`, `.gram.kanban.md` (Tasks 0.8, 0.9) and `docs/gram-ide-projects.md`.

### How the work is split

- The user writes what has learning value (here: the `Msvc` arm of `toolchain::settings` and anything else that's new MSVC/Visual Studio knowledge), Claude Code writes the plumbing (detection, flags, tests, docs) and reviews the user's code. The user wants short, exact instructions ("what to write, where, what the test expects"), and explanations at a junior level for build-system topics (CMake, MSVC flags).
- Commit only when asked; `.vscode/settings.json` holds machine-local settings and is never committed. Commands for the user to copy carry no inline comments.

### The machine

- Visual Studio 2026 with the *Desktop development with C++* workload (MSVC, the Windows SDK, its own CMake).
- The Vulkan SDK (`VULKAN_SDK` set by its installer; `dxc` is found from it).
- vcpkg, with `VCPKG_ROOT` set.
- rustup: `x86_64-pc-windows-msvc` is the host default; add `x86_64-pc-windows-gnu` only to test the mingw fallback.
- Bootstrap: `cargo build --manifest-path gram/Cargo.toml`, then `gram\target\debug\gram.exe build` from the repo root.

### Steps, in order

0. **Try it as it is**, from a *Developer PowerShell for VS*: `gram build`. The `Msvc` arm sets nothing, so CMake picks the Visual Studio generator and vcpkg `x64-windows` on its own; `artifact_candidates` already looks in the multi-config `<build dir>/<Config>/`. Write down what breaks (code that GCC/clang accept and MSVC doesn't: see above) before changing anything.
1. **Detection with vswhere** (Claude, `toolchain::installed`, `Msvc` arm): `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`, so a plain PowerShell finds Visual Studio too, not only a Developer Console (`cl` on the `PATH`, today's check). The Visual Studio generator doesn't need the vcvars environment; Ninja would (then add an `env` field to `ToolchainSettings`, filled from `vcvars64.bat`).
2. **Msvc settings** (user, `toolchain::settings`, with a test like `mingw_cross_compile_matches_todays_build`): the generator (check the exact name with `cmake --help`: `Visual Studio 18 2026` expected), `-A x64`, the vcpkg triplet `x64-windows-static` and `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>` together (a static CRT on both sides, like mingw's standalone `.exe`; mixing `/MT` and `/MD` fails at link time), `cargo_triple: None` (the host's msvc triple). The `.cargo/config.toml` linker entry is for `-gnu` only and doesn't apply.
3. **The configurations under MSVC**: `GramConfigurationFlags.cmake` derives Final from Release's flags and Release from RelWithDebInfo's, compiler-independently; check the cache (`CMAKE_CXX_FLAGS_FINAL` ≈ `/O2 /Ob2 /DNDEBUG`, Release with `/Zi`), that `CMAKE_INTERPROCEDURAL_OPTIMIZATION_FINAL` turns into `/GL` + `/LTCG`, and that Final links vcpkg's release libraries (`CMAKE_MAP_IMPORTED_CONFIG_FINAL`).
4. **Subsystem**: a console application today (a console window with the logs next to the game). Decide on purpose for Final (`/SUBSYSTEM:WINDOWS` needs `WinMain`, which `EntryPoint.hpp` already has).
5. **The mingw fallback on Windows**: `.cargo/config.toml` names the `x86_64-pc-windows-gnu` linker `x86_64-w64-mingw32-gcc`, which a native mingw (MSYS2, w64devkit) calls `gcc`: override it (`CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER=gcc`, through an `env` field) and check `mingw32-make` is on the `PATH` for the `MinGW Makefiles` generator.
6. **`gram ide vs`** (Claude; the code exists, `config/ide.rs`, untested): check
   - the generator name, and the solution's file name: `ide()` returns `<project>.sln`, but CMake may write `.slnx` for Visual Studio 2026;
   - the `PATH` baked into the `GameLogic` target (`cmake -E env "PATH=..."`): Windows separates it with `;`, which CMake may split into a list; if it does, escape it (`$<SEMICOLON>`);
   - the startup project: add `set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT <runtime>)` to the superproject, so F5 starts the game, not `ALL_BUILD`;
   - the debugger's working directory doesn't matter (the game finds its files next to the executable, `GetExecutableDirectory`), the `.pdb` stays in the build directory, where the debugger finds it;
   - hot reload: building `GameLogic` while the game runs (the loader copies the `.dll` before loading it, so the file isn't locked).

### Test matrix

| Where | Command | Expected |
|---|---|---|
| WSL2 (a Linux host) | `gram build --target windows` | mingw, as before: `Game.exe` runs on Windows |
| WSL2 | `gram build --toolchain msvc --target windows` | error: msvc can't build target windows from a linux host |
| Developer PowerShell | `gram build` | msvc, no warning |
| PowerShell, Visual Studio installed | `gram build` | msvc (step 1) |
| PowerShell, only mingw | `gram build` | warning, then mingw (step 5) |
| PowerShell, only mingw | `gram build --toolchain msvc` | error: msvc isn't installed, no fallback |
| PowerShell | `gram build --config release`, `--config final` | both build and run; Final without ImGui |
| PowerShell | `gram ide vs --open` | the solution opens; F5 starts `Game` at a breakpoint; building `GameLogic` reloads it |
| any | `cargo test --manifest-path gram/Cargo.toml` | passes (the `build`/`run` tests need cmake and a C++ compiler) |

Then re-check Linux on a real machine (the section above).
