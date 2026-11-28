Gram Engine 0.1.0 - Windows (x64) - {CONFIG}
==========================================

A preview build of Gram, a personal game engine (C++, Vulkan, Rust).
What you'll see is its path tracer: spheres in a scene lit by a sky.

Requirements
------------
- 64-bit Windows 10 or 11.
- A graphics driver with Vulkan: any current NVIDIA, AMD or Intel driver
  installs it.
Nothing else to install.

Running it
----------
1. Extract the zip somewhere on your disk. The game writes a "hot" folder next
   to itself, so it can't run from inside the zip or a read-only place.
2. Double-click Game.exe.
3. The first time, Windows may say "Windows protected your PC": this build
   isn't signed. Click "More info", then "Run anyway".
4. On Windows 11, Smart App Control may block GameLogic.dll, the game's logic
   ("An Application Control policy has blocked this file" in the log): the
   scene still shows, but nothing moves.
{CONFIG_NOTES}
Closing: Esc, or the X at the right of the menu bar.

Files
-----
Game.exe                     the engine
GameLogic.dll                the game's logic (Rust)
shaders\, textures\          what the game loads

License
-------
Gram is MIT licensed (LICENSE.txt). The libraries it includes and their
licenses are listed in THIRD_PARTY_NOTICES.txt. The sky (Poly Haven) and the
statue photo (Pixabay) are CC0.
