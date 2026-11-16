# Plan — configurații de build, toolchain-uri, proiecte IDE (Tasks 0.8, 0.9 + una nouă)

## Context

Până acum `gram build` face un singur fel de build (Debug, `BUILD_CONFIG` în `gram/gram-common/src/lang/cmake.rs`), cu compilatorul ales implicit de CMake pe host și cu mingw cablat doar pe ramura de cross-compile (`"windows"` din `cmake.rs`, `cargo_cross_triple` din `target.rs`). Debugging-ul a mers abia după ce s-a dat `CMAKE_BUILD_TYPE=Debug`, iar pe Windows/WSL2 alegerea compilatorului a fost o sursă de probleme. Scopul:

1. **Debug / Release / Final** — baza pentru wave-ul de packaging.
2. **Noțiunea de toolchain** — ales implicit după host, suprascris explicit (`--toolchain`), cu fallback (Windows: MSVC → mingw).
3. **`gram ide xcode|vs`** — un singur proiect IDE pentru tot jocul (C++ + Rust), cu build și debugging din IDE.

Ordinea aleasă: configurații → toolchain (comportamentul de azi ca default) → Xcode → (mai târziu, pe Windows) VS + MSVC + fallback. Doar macOS e disponibil acum, deci fazele 1–3 trebuie să fie testabile complet aici; faza 4 se scrie și se testează pe Windows.

**Împărțirea muncii (mixt):** fiecare pas e marcat **[Tu]** (ceva nou de învățat: modelul de date, maparea, generatorul) sau **[Claude]** (plumbing: flag-uri clap, căi, teste, `.vscode`, docs). Claude face review după fiecare pas [Tu], și ne oprim după fiecare fază.

---

## Faza 1 — Configurațiile Debug / Release / Final

Semantica (aleasă):
| | C++ | Rust | Engine |
|---|---|---|---|
| **Debug** | `-O0 -g` | profilul `dev` | editor (ImGui), log, validation layers |
| **Release** | optimizat **+ simboluri** | `--release` + `CARGO_PROFILE_RELEASE_DEBUG=true` | editor, log; fără validation |
| **Final** | optimizat, LTO, strip | `--release` + `CARGO_PROFILE_RELEASE_LTO=true`, `..._STRIP=true` | **fără editor/ImGui**, fără validation |

Pentru cargo se folosesc variabilele de mediu `CARGO_PROFILE_RELEASE_*` în loc de un `[profile.final]`: nu trebuie editat niciun `Cargo.toml` (profilurile contează doar la rădăcina unui workspace), iar calea artefactului vine deja din `--message-format=json` (`cargo.rs`), deci `target/release` merge fără altă schimbare.

### Pași

1. **[Tu]** `gram-common/src/profile.rs`: `enum Profile { Debug, Release, Final }` (numele `Config` e deja ocupat de `BuildConfig`/`config/`), `FromStr`/`Display`, și o mapare ca date: `cmake_config()` (`"Debug"`/`"Release"`/`"Final"`), `cargo_args()` + `cargo_env()`. Funcții pure, cu teste unitare.
2. **[Claude]** `--config debug|release|final` (implicit `debug`) pe `BuildCommand` și `RunCommand`; `Profile` dus prin `BuildConfig::build` → `ModuleConfig::build_target` → `ModuleBackend::compile` (semnătura primește `profile`). În `cmake.rs`, `BUILD_CONFIG` dispare în favoarea `profile.cmake_config()`; build dir-ul devine `build-<target>-<profile>` (schimbarea configurației nu mai strică obiectele celeilalte).
3. **[Claude]** Căile de output: `build/<project>/<target>/<profile>/` (și `build/<project>_<version>_<target>_<profile>/` la `--ci`), plus `ModuleConfig::run`. Actualizate `.vscode/launch.json` / `tasks.json` și testele din `gram/gram-cli/tests/` (`build.rs`, `run.rs`: un test `--config release`, unul cu o configurație necunoscută).
4. **[Tu, cu Claude pentru CMake]** `GramEngine/GramConfigurations.cmake`: declară configurația `Final` pentru CMake — `CMAKE_CXX_FLAGS_FINAL`/`CMAKE_EXE_LINKER_FLAGS_FINAL` (= Release + LTO/strip), `CMAKE_CXX_FLAGS_RELEASE` cu `-g`, `CMAKE_MAP_IMPORTED_CONFIG_FINAL=Release` (pentru dependențele vcpkg), `CMAKE_CONFIGURATION_TYPES=Debug;Release;Final` pentru generatoarele multi-config. Trebuie inclus înainte de primul `project()`; gram îl dă cu `-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=…` (CMake ≥ 3.24, de verificat versiunea instalată). Ștergem `set(GRAM_IMGUI_ENABLED)` rătăcit din `GramEngine/CMakeLists.txt`.
5. **[Tu]** Macro-urile de configurație, după convenția din `Common.hpp`: CMake definește `GRAM_CONFIG_DEBUG`/`_RELEASE`/`_FINAL` prin **generator expressions** (`$<$<CONFIG:Final>:GRAM_CONFIG_FINAL>`, `PUBLIC` pe `GramEngine`) — obligatoriu, ca să meargă și în Xcode/VS, unde configurația se alege la build, nu la configure. `Gram/Common.hpp` derivă din ele `GRAM_IMGUI_ENABLED` (nu în Final), `GRAM_LOGGER_ENABLED` și un `GRAM_VULKAN_VALIDATION` (doar Debug), pe care îl citește `VulkanRendererSpecification` (`Renderer/Common.hpp`, `m_RequiredValidationLayers`).
6. **[Tu]** Jocul în Final, fără editor: azi scena se vede doar prin `ViewportPanel`. Final are nevoie de o trecere care copiază ținta scenei în swapchain (un `PresentPass`/blit, ultimul pass când ImGui lipsește), iar `main.cpp` își îngrădește `Editor`/`GameUI` cu `GRAM_IMGUI_ENABLED`. Pas separat: Final poate exista ca build înainte să arate ceva pe ecran.

**Verificare:** `cargo test --manifest-path gram/Cargo.toml`; `gram build --config debug|release|final` fiecare urmat de `gram run --config …` (Release: editor + FPS vizibil mai mare decât Debug pe scena finală, ~8 FPS de referință; Final: fereastra fără ImGui, pasul 6); F5 în VS Code tot oprește la breakpoint.

---

## Faza 2 — Noțiunea de toolchain (fără să schimbe rezultatele de azi)

1. **[Tu]** `gram-common/src/toolchain.rs`: `enum Toolchain { Native, Mingw, Msvc }` (`Native` = ce alege CMake pe host: Apple clang, GCC) și `fn resolve(host, target, requested: Option<Toolchain>, probe: &dyn Fn(&str) -> bool) -> io::Result<Toolchain>`:
   - explicit (`--toolchain mingw`) → fără fallback, eroare clară dacă lipsește;
   - implicit: target == host → `Native` (pe Windows: `Msvc`, cu **fallback la `Mingw`** + warning dacă `probe` nu găsește MSVC); Windows de pe Linux/macOS/WSL2 → `Mingw`; linux ↔ macos → eroare (ce face azi `buildable_on_host`).
   - `probe` injectat (în producție: căutare în `PATH`, mai târziu `vswhere`) ca să fie testat pe macOS cu probe false.
2. **[Tu]** `struct ToolchainSettings { generator, cmake_args, vcpkg_triplet, cargo_triple, env }` din `(target, toolchain, profile)` — singurul loc care știe de `x86_64-w64-mingw32-g++`, `x64-mingw-static`, `-static-libgcc …`, `x86_64-pc-windows-gnu`. Ramurile `Msvc` returnează deocamdată eroarea „neimplementat” (faza 4).
3. **[Claude]** Mutarea apelanților: ramura `"windows"` din `CMake::compile` și `cargo_cross_triple`/`buildable_on_host` din `target.rs` citesc `ToolchainSettings`; `--toolchain` pe `build` (și pe `ide`). Testele de comandă existente trebuie să treacă neschimbate (asta e dovada că defaulturile = comportamentul de azi); teste unitare de tip tabel pentru `resolve` și `ToolchainSettings`.

**Verificare:** `cargo test`; `gram build` și `gram build --ci` pe macOS dau aceleași artefacte ca înainte (inclusiv `.exe`-ul mingw); `gram build --target windows --toolchain msvc` pe macOS → eroare explicită.

---

## Faza 3 — `gram ide xcode`

Ideea (din `docs/gram-ide-projects.md`): gram generează un **superproiect CMake** în `build/ide/xcode/` și îl configurează cu `-G Xcode`; CMake scrie `.xcodeproj`.

1. **[Claude]** `commands/ide.rs`: `gram ide <xcode|vs> [--open] [--toolchain]` (`vs` refuzat pe macOS). Configurează cu toolchain-ul vcpkg și un manifest vcpkg combinat al tuturor modulelor cmake (extinderea `vcpkg::build_manifest`, care azi combină manifestele unui modul + referințele lui), plus `GramConfigurations.cmake` din faza 1 → meniul de scheme din Xcode are Debug/Release/Final.
2. **[Tu]** Generatorul superproiectului (`BuildConfig::ide_project()` → text, ca emițătoarele din `schema/`):
   - `add_subdirectory` pentru fiecare modul cmake **nereferențiat** de alt modul cmake (azi doar `Game/Runtime`; `GramEngine` vine prin el — altfel ar fi adăugat de două ori);
   - pentru fiecare modul cargo, un `add_custom_target(GameLogic …)` care rulează `gram build --module GameLogic --config $<LOWER_CASE:$<CONFIG>>` (refolosește generarea schemelor — `Runtime` include `Components.generated.hpp` — numirea `libGameLogic.dylib` și cargo) și copiază biblioteca în `$<TARGET_FILE_DIR:Runtime>`; calea lui gram = `std::env::current_exe()` la generare;
   - `add_dependencies(Runtime GameLogic)` pentru executabilele care îl încarcă (deocamdată: toate runtime-urile cmake ale aceluiași proiect gram);
   - post-build pe executabil: `shaders/` și `textures/` din `${CMAKE_BINARY_DIR}` lângă el (aceeași listă ca `ASSET_DIRS` din `cmake.rs`), și `OUTPUT_NAME` = numele lui gram (`Game`).
3. **[Claude]** Detalii Xcode: `CMAKE_XCODE_GENERATE_SCHEME ON`, `XCODE_SCHEME_WORKING_DIRECTORY` = directorul executabilului, executabilul ca țintă implicită de Run; `--open` → `open build/ide/xcode/<project>.xcodeproj`.
4. **[Claude]** Teste: un test de comandă care verifică doar textul generat (fără Xcode); unul `#[ignore]`d care configurează cu `-G Xcode`.

**Verificare (manual, în Xcode):** `gram ide xcode --open`; ierarhia GramEngine/Runtime/GameLogic vizibilă; Run în Debug cu breakpoint în `RayTracePass.cpp`; schimbare în `Game/Content/scripts`, Build pe ținta `GameLogic` cât timp jocul rulează → reload în Log; schema Release și Final compilează și pornesc.

---

## Faza 4 — Windows (mai târziu, pe mașina țintă)

- **[Tu/Claude]** `Toolchain::Msvc` în `ToolchainSettings`: generator Visual Studio 18 2026, triplet `x64-windows-static`, `x86_64-pc-windows-msvc`, CRT `/MT` consecvent, subsystem decis explicit; detecție cu `vswhere`, fallback la mingw cu warning; `gram build` dintr-un terminal obișnuit (mediul `vcvars` din `vswhere`).
- **[Claude]** `gram ide vs`: același superproiect, plus `VS_STARTUP_PROJECT` și `VS_DEBUGGER_WORKING_DIRECTORY`.
- Lista de verificat e deja în `docs/gram-native-toolchains.md` („Suggested order”).

## Documentație (Claude, la finalul fiecărei faze)

`.gram.kanban.md` (task nou 0.10 pentru configurații; 0.8/0.9 actualizate), `docs/gram-ide-projects.md`, `docs/gram-native-toolchains.md`, `README.md` (flag-urile noi), secțiunea Gram Build din `CLAUDE.md`.

## Fișiere principale

- `gram/gram-common/src/lang/cmake.rs`, `lang/cargo.rs`, `lang/mod.rs` (`ModuleBackend::compile`), `lang/vcpkg.rs`
- `gram/gram-common/src/config/mod.rs` (`build`), `config/module.rs` (`build_target`, `run`), `target.rs`
- noi: `profile.rs`, `toolchain.rs`, `commands/ide.rs`
- `gram/gram-cli/tests/` (`build.rs`, `run.rs`, nou `ide.rs`)
- `GramEngine/CMakeLists.txt`, nou `GramEngine/GramConfigurations.cmake`, `GramEngine/include/Gram/Common.hpp`, `Renderer/Common.hpp`
- `Game/Runtime/src/main.cpp`, `.vscode/launch.json`, `.vscode/tasks.json`
