Gram Engine 0.1.0 - macOS (Apple Silicon) - {CONFIG}
==================================================

A preview build of Gram, a personal game engine (C++, Vulkan, Rust).
What you'll see is its path tracer: spheres in a scene lit by a sky.

Requirements
------------
- A Mac with Apple Silicon (M1 or later; Intel Macs aren't supported).
- macOS 26 or later.
Nothing else to install: the Vulkan loader and MoltenVK (Vulkan on top of
Metal) are in this folder.

Running it
----------
1. Copy this folder somewhere on your disk (Downloads is fine). The game
   writes a "hot" folder next to itself, so it can't run from a read-only place.
2. Double-click run.command. Not Game: run.command tells Vulkan where to find
   the MoltenVK in this folder, and Game alone can't find it.
3. The first time, macOS may refuse to open it: this build isn't notarized by
   Apple. Open System Settings > Privacy & Security, scroll down and click
   "Open Anyway", then double-click run.command again.
   Or, in Terminal, inside this folder:  xattr -cr .
{CONFIG_NOTES}
Closing: Esc, or the X at the right of the menu bar.

Files
-----
run.command                  starts the game
Game, libGameLogic.dylib     the engine and the game's logic (Rust)
shaders/, textures/          what the game loads
libvulkan.1.4.363.dylib      the Vulkan loader
libMoltenVK.dylib            MoltenVK, Vulkan on Metal
MoltenVK_icd.json            tells the loader where MoltenVK is
{CONFIG_FILES}
License
-------
Gram is MIT licensed (LICENSE.txt). The libraries it includes and their
licenses are listed in THIRD_PARTY_NOTICES.txt. The sky (Poly Haven) and the
statue photo (Pixabay) are CC0.
