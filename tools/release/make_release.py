#!/usr/bin/env python3
"""Assembles the four preview packages of Gram 0.1.0 (macOS/Windows x Debug/Release) and zips them into
build/release/0.1.0/. Run from the repository root, after `gram build` for macos and windows, Debug and Release.

The macOS folders come from build/package/gram-engine-macos-<config>/, prepared by hand on 2026-11-28 (see
docs/gram-native-toolchains.md, *A preview release*): Game's rpath set to @executable_path and Game re-signed
ad hoc, the Vulkan loader, MoltenVK (and in Debug the validation layer, thinned to arm64) next to it, their JSON
manifests, and run.command. A later `gram package` would do those steps itself.

Next to this script: README-<platform>.txt (the templates) and imgui-<config>.ini, the panels' default layout
shipped as imgui.ini (arrange them once, close the game, copy its imgui.ini here)."""
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path.cwd()
HERE = Path(__file__).resolve().parent
OUT = ROOT / "build/release/0.1.0"
SHARE_MAC = ROOT / "Game/Runtime/build-macos-release/vcpkg_installed/arm64-osx/share"
SPDLOG = ROOT / "Game/Runtime/build-macos-release/_deps/spdlog-src"

NOTES = {
    "Release": """
What to do
----------
The scene starts with the sky only. In the menu bar, open Game > Debug >
Ray Tracer and click "Generate Scene". The small spheres bob up and down:
that's the game's logic, in Rust.
- Game > Debug > Ray Tracer: the camera, the quality, the BVH.
- Game > Debug > Display: the resolution, windowed or fullscreen, V-Sync, and
  Take Picture (a PNG in screenshots/, next to the game).
- Engine > Profiler: the GPU time of each pass; Record saves a benchmark.
""",
    "Debug": """
What to do
----------
This is the editor: Outliner, Inspector, Log, Profiler and the Viewport. The
scene starts with the sky only: open Game > Debug > Ray Tracer and click
"Generate Scene", then select a sphere in the Outliner to edit it in the
Inspector.
The game starts stopped: press Play in the toolbar to run the game's logic
(the small spheres bob up and down), Stop to put the scene back. On the
developer's machine Play first rebuilds that logic; the tools for it aren't
included, so here it plays what's loaded, and the Log says so.
{VALIDATION}
""",
}
VALIDATION = {
    "macos": "Vulkan's validation layer is on and included: its messages go to the Log.",
    "windows": "Vulkan's validation layer comes with the Vulkan SDK, which isn't included:\n"
               "without it the Log shows \"validation layer not supported\" once and the\n"
               "game goes on.",
}
MAC_DEBUG_FILES = ("libVkLayer_khronos_validation.dylib  Vulkan's validation layer\n"
                   "VkLayer_khronos_validation.json      tells the loader where it is\n")


def comment_body(path: Path, start: str, stop: str) -> str:
    """A license kept in a source file's header comment, without the comment marks."""
    text = path.read_text(encoding="utf-8", errors="replace")
    block = text[text.index(start): text.index(stop) + len(stop)]
    lines = [re.sub(r"^\s*(//|\*|/\*)?\s?", "", line).rstrip() for line in block.splitlines()]
    return "\n".join(lines)


def notices(platform: str, config: str) -> str:
    sections = [
        ("GLFW (window and input)", (SHARE_MAC / "glfw3/copyright").read_text()),
        ("Dear ImGui (the panels)", (SHARE_MAC / "imgui/copyright").read_text()),
        ("GLM (math)", (SHARE_MAC / "glm/copyright").read_text()),
        ("stb_image, stb_image_write (images)", (SHARE_MAC / "stb/copyright").read_text()),
        ("spdlog (logging)", (SPDLOG / "LICENSE").read_text()),
        ("{fmt} (bundled with spdlog)",
         comment_body(SPDLOG / "include/spdlog/fmt/bundled/format.h", "Copyright (c) 2012", "THE SOFTWARE.")),
        ("Vulkan-Headers", (SHARE_MAC / "vulkan-headers/copyright").read_text()),
        ("AMD FidelityFX Super Resolution 1 (compiled into shaders/)",
         comment_body(ROOT / "Game/Content/shaders/ffx/ffx_fsr1.h", "Copyright (c) 2021 Advanced", "THE SOFTWARE.")),
        ("The Rust standard library (in the game's logic)",
         "Copyright The Rust Project Developers.\nDual-licensed under the MIT license or the Apache License 2.0 "
         "(the Apache text is under Vulkan-Headers above).\nhttps://github.com/rust-lang/rust"),
    ]
    if platform == "macos":
        sections += [
            ("Vulkan loader (libvulkan.1.4.363.dylib)", (SHARE_MAC / "vulkan-loader/copyright").read_text()),
            ("MoltenVK (libMoltenVK.dylib)",
             "Copyright (c) 2015-2026 The Brenwill Workshop Ltd. (http://www.brenwill.com)\n"
             "Licensed under the Apache License, Version 2.0 (the text is under Vulkan-Headers above).\n"
             "https://github.com/KhronosGroup/MoltenVK"),
        ]
        if config == "Debug":
            sections.append(("Vulkan Validation Layers (libVkLayer_khronos_validation.dylib)",
                             "Copyright The Khronos Group Inc., Valve Corporation, LunarG, Inc.\n"
                             "Licensed under the Apache License, Version 2.0 (the text is under Vulkan-Headers above).\n"
                             "https://github.com/KhronosGroup/Vulkan-ValidationLayers"))
    text = "Third-party software in this build of Gram\n==========================================\n"
    for title, body in sections:
        text += f"\n\n{'-' * 78}\n{title}\n{'-' * 78}\n\n{body.strip()}\n"
    return text


def readme(platform: str, config: str) -> str:
    template = (HERE / f"README-{platform}.txt").read_text()
    notes = NOTES[config].replace("{VALIDATION}", VALIDATION[platform])
    files = MAC_DEBUG_FILES if (platform, config) == ("macos", "Debug") else ""
    return template.replace("{CONFIG}", config).replace("{CONFIG_NOTES}", notes).replace("{CONFIG_FILES}", files)


def package(platform: str, config: str) -> Path:
    arch = "arm64" if platform == "macos" else "x64"
    name = f"gram-engine-0.1.0-{platform}-{arch}-{config.lower()}"
    folder = OUT / name
    if folder.exists():
        shutil.rmtree(folder)
    if platform == "macos":
        shutil.copytree(ROOT / f"build/package/gram-engine-macos-{config.lower()}", folder, symlinks=True)
    else:
        built = ROOT / f"build/gram-engine/windows/{config.lower()}"
        folder.mkdir(parents=True)
        for file in ("Game.exe", "GameLogic.dll"):
            shutil.copy2(built / file, folder)
        for sub in ("shaders", "textures"):
            shutil.copytree(built / sub, folder / sub)
    # The panels' default layout, so the first start doesn't pile them up; the game rewrites it as it's used.
    layout = HERE / f"imgui-{config.lower()}.ini"
    (folder / "imgui.ini").unlink(missing_ok=True)
    if layout.exists():
        shutil.copy2(layout, folder / "imgui.ini")
    newline = "\r\n" if platform == "windows" else "\n"  # Notepad reads either; CRLF for the Windows ones anyway
    for file, text in (("README.txt", readme(platform, config)),
                       ("THIRD_PARTY_NOTICES.txt", notices(platform, config)),
                       ("LICENSE.txt", (ROOT / "LICENSE").read_text())):
        (folder / file).write_bytes(text.replace("\r\n", "\n").replace("\n", newline).encode("utf-8"))
    archive = OUT / f"{name}.zip"
    archive.unlink(missing_ok=True)
    # ditto keeps the executable bits; --norsrc leaves out macOS's ._ metadata files.
    subprocess.run(["ditto", "-c", "-k", "--norsrc", "--keepParent", str(folder), str(archive)], check=True)
    return archive


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for platform in ("macos", "windows"):
        for config in ("Debug", "Release"):
            archive = package(platform, config)
            print(f"{archive.relative_to(ROOT)}  {archive.stat().st_size / 1e6:.1f} MB")
