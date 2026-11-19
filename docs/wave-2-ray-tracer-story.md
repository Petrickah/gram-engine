# Wave 2 — the ray tracer, the story so far

A chronological account of 2026-11-05 → 2026-11-19: from a triangle with no buffers to *Ray Tracing in One Weekend* running in a compute shader, up to the book's cover scene, then the spheres becoming entities that Rust moves and every shader's data described once in a `.ron` file, and a BVH that turned out to fix less than expected, with progressive accumulation, supersampling and depth of field done twice (a real lens, then a post-process). It exists to keep the sequence straight for the Episode 2 devlog: what was tried, what broke, what each bug taught. The technical reference for the renderer as built is `docs/wave-2-renderer-guide.md`; this is the narrative.

Method throughout, as for every wave: the user wrote the Vulkan and shader code (the learning), Claude reviewed, explained, and wrote the plumbing (CMake, the `gram` tool, wiring passes together).

## Part 1 — Vulkan groundwork (Nov 5–10)

Wave 1 ended with a triangle whose vertices were hard-coded in the shader. A ray tracer needs data flowing from C++ to the GPU (a camera, a list of spheres) and an image a compute shader can write, so the first week was the Khronos tutorial's buffer, uniform and texture chapters, bent to fit the engine.

**Frames and passes (Nov 6).** The renderer was split into `VulkanFrame` (command buffers, two frames in flight, sync) and passes (`VulkanPass::Record`, registered by the game through `VulkanRenderer::AddPass`). The engine knows no shader of the game's; ImGui is itself the last pass. Compute pipelines arrived next to graphics ones.

**A scene target and a viewport (Nov 7).** Instead of drawing into the swapchain, the game draws into `VulkanRenderTarget`, an image shown by the editor's Viewport panel and recreated when the panel is resized. `VulkanImage::State` tracks each image's last use so `Transition` can emit the right barrier. The rule that would come back later: no barrier can go inside `beginRendering`/`endRendering`, so an image must be left ready to be sampled before ImGui draws.

**Buffers, the C++20 way (Nov 7–9).** A vertex buffer, then a staging upload into device-local memory, then an index buffer for a quad. The interesting part was the API: one `VulkanBuffer::Create<Kind>` described by a `BufferKind` concept (the first concept written by hand), with `VertexBuffer`/`IndexBuffer` staged and `UniformBuffer<T>`/`StorageBuffer<T>` host-visible, one per frame in flight. Mapping memory returns a move-only token that unmaps in its destructor and whose `Write`/`Read` exist only for the access mode it was opened with (`requires` clauses).

**Uniforms and the descriptor wall (Nov 9–10).** A rotating quad needed a per-frame uniform, and the validation layer pushed back: an empty pipeline layout, two layouts each claiming both sets, a descriptor pool destroyed as a local variable while its sets were still in use. The fix became a class, `VulkanDescriptors`: one layout from a pass's bindings, its own pool, one set per frame in flight, written once at creation. The mental model that stuck: layout = shape, set = instance, the pool owns the sets.

**Textures (Nov 10).** `stb_image` moved into the engine (`VulkanImage::Load`), and the quad got a photo. A side quest: `gram` was corrupting hand-written entries in `vcpkg.json` when it edited the manifest; fixed in `lang/vcpkg.rs` with a test. Also: VS Code's IntelliSense reported errors the compiler didn't (designated initializers, overloaded templates); clangd replaced it.

**The first compute dispatch (Nov 10).** `RayTracePass` wrote the scene target as a storage image. After a resize the image view was stale; the fix is the pattern used by every pass since: the target has a *generation* bumped on recreation, and a pass rewrites its descriptor when the generation changes (safe because the resize waits for the device first).

## Part 2 — Ray Tracing in One Weekend, on the GPU (Nov 11–12)

The book is CPU C++ writing a PPM file. Porting it to a compute shader changes three things: no recursion in HLSL (bounces become a loop with a limit), no `std::rand` (a hash-based generator seeded per pixel and per frame), and every piece of data crosses from C++ through a buffer whose layout must match byte for byte (`static_assert` on every struct, on both sides).

**Camera (ch. 4, 12).** Building the camera from view/projection matrices fought with the ImGui controls; a camera from basis vectors (`Forward`, `Right`, `Up`, plus `tan(fov/2)` and the aspect ratio) was simpler and is what the shader needs anyway. Yaw/pitch/FOV sliders made chapter 12 unnecessary.

**Spheres (ch. 5–6).** Spheres live in a `StructuredBuffer`. Two bugs worth telling: a removed sphere stayed on screen, because `GetDimensions` returns the buffer's *capacity*, not how many spheres are alive (fixed by sending `SphereCount`); and far spheres drew over near ones, until the closest hit so far was passed as the next sphere's `tmax`.

**Random numbers (ch. 8).** A black image, until the shader got a PCG hash, seeded from the pixel and the frame. Later bug: the seed used `FrameIndex`, which is the frame-in-flight slot (0 or 1), so the noise repeated every two frames; a real frame counter fixed it.

**Antialiasing, and a detour through "FSAA".** A first post-process attempt failed on validation errors: transitions inside dynamic rendering, and a pass reading and writing the same image (a feedback loop). Separate input and output images fixed it. Then a shader suggested by another AI as "FSAA" turned out to be a blur: averaging neighbours at the same resolution is a tent filter. Real supersampling means rendering *more* pixels: the ray tracer now renders at 2× and an `SSAA` pass resolves 2×2 blocks into the scene target.

**A generic post-process pass.** `PostProcessPass<Params>`: a fullscreen quad, a shader, and a `Fill` callback returning the pass's uniform each frame, so a new effect is a shader plus a lambda, no new class. Chaining passes exposed ownership: render targets became `std::shared_ptr`s passed between passes, and the first version crashed because a constructor did `std::move` on a `shared_ptr&` that was the renderer's own scene target, emptying it.

**Progressive accumulation.** Each frame is noisy; the average of many frames isn't. The accumulation pass needs no shader math: the pipeline's blending (`SrcAlpha`/`OneMinusSrcAlpha`) with alpha = 1/(n+1) *is* a running average. Lessons: the first frame (alpha = 1) must overwrite; a new image must be cleared once (uninitialized memory can hold NaN, and NaN never leaves an average); and any change to the scene must reset n, or a newly added sphere fades in "from nothing" over hundreds of frames (the camera sliders reset it, the Add/Remove buttons didn't, at first).

**Diffuse (ch. 9).** With the jitter in place, diffuse bounces made accumulation visible: the image converges from noise. Details that mattered on the GPU: a random unit vector without a rejection loop (z uniform in [-1, 1], an angle around Z: no divergent loop between threads); `Normal + random` can be zero and `normalize(0)` is NaN; and gamma correction (9.5) was already done by the hardware, since the swapchain is `B8G8R8A8Srgb`. Applying the book's `sqrt` on top gave a washed-out grey image; averaging must also happen in linear space, before any gamma.

**Materials (ch. 10).** Albedo moved out of the shader into a `StructuredBuffer<Material>` indexed by each sphere, which is the same shape a rasterizer's bindless materials take (a detour into how raster engines do materials: shader + parameters, sets per frame/material/object). A `switch` on the material type inside the bounce loop replaced the book's virtual `scatter`. Bug: removing a sphere erased its material and shifted every later index. Metal: `reflect` plus fuzz, absorbed when fuzz pushes the ray under the surface.

**Glass (ch. 11).** Refraction, total internal reflection, Schlick. Two extensions beyond the book. Hollow spheres as one shape: what decides the side of a refraction is the direction of the normal, so the inner surface of a shell gets a normal pointing to the center, and one index of refraction works for both walls, instead of the book's second sphere with an inverted index (verified by rendering both side by side). And tinted glass by distance (Beer–Lambert, `Albedo^(distance / AbsorptionDistance)`) instead of multiplying by the albedo at every surface, which had produced hard edges where the number of surfaces a ray crossed jumped from two to four. The concentric "repeated spheres" inside a thick shell turned out to be physics: total internal reflection between the cavity and the outer wall, one image per extra pair of bounces, visible by raising the bounce limit one step at a time.

## Part 3 — Depth of field, twice (Nov 13)

**Through a lens (ch. 13).** First attempt: the aperture scaled the pixel jitter (a uniform screen blur, not depth of field) and the ray's origin moved without its direction changing (parallel rays meet nowhere, so nothing is sharp). The real thing: compute where the pinhole ray meets the focus plane, then shoot from a random point on a lens disk (in the camera's plane, uniform on its area) toward that point.

Then units. Mixing "focal length" (a lens property, millimetres) with "focus distance" (where the sharp plane is, metres) produced a 2.2 mm lens: a 160° fisheye and no blur at all. The camera now behaves like a real one: the focal length a full-frame sensor needs for the chosen FOV, an f-stop (lens radius = f / 2N), a focus distance in metres. A real camera's blur at f/5.6 is subtle, so a "bokeh scale" exaggerates it on purpose.

**As a post-process.** How games do it, since a rasterizer has no lens: render sharp, then blur each pixel by its circle of confusion (CoC), computed from its depth. That needed a depth output from the ray tracer: one pinhole ray through the pixel's center, its first hit's distance along the camera's axis (`t * dot(dir, forward)`), never accumulated (averaging two depths at an edge gives a depth nothing has). Bugs along the way: the depth descriptor written with the color image; one generation counter shared by two images, so the depth's descriptor was never refreshed; the depth taken from the *last* bounce instead of the first. `PostProcessPass` learned to take several inputs (an unsigned loop counting down to `>= 0` never ended).

The shader: CoC = `CoCScale * |d - focus| / d` (the lens's physics folded into one constant on the CPU), a gather over 48 points on a golden-angle spiral, and a weight per neighbour: it contributes only if its own CoC reaches this pixel, so a sharp subject doesn't bleed into the blurred background behind it.

**The comparison.** At equal settings both look alike; at high bokeh the post-process blurs less, because its CoC is capped (`MaxCoC`, which bounds the loop) and 48 samples on a wide disc thin out, while the ray tracer pays for blur over time through accumulation. And the post-process can't show what's behind a blurred foreground object: one depth per pixel, no information about what it hides. That limitation is the whole reason the comparison was worth doing.

**The book's cover (ch. 14).** The closing image: a ground, a 22×22 grid of small spheres with random materials, three big ones (glass, diffuse, mirror), generated on the CPU with a seeded `std::mt19937` so the scene is the same every run; the shader didn't change at all. It's also the first time cost showed up: 486 spheres, every ray testing every one of them at every bounce, ~8 FPS on the M4 Pro (one sample per frame, 10 bounces, twice the viewport's resolution). That number is the baseline for the bounding volume hierarchy that comes next.

## Part 4 — The scene becomes the engine's (Nov 17–18)

Between Nov 13 and 17 the work left the ray tracer (build configurations, Xcode and Visual Studio projects, Play/Pause/Stop in the editor, running on SteamOS: other stories). It came back with one goal: the spheres had to stop being a `std::vector` inside the ray tracer and become what Wave 1 built the engine for, entities of the `World`, edited by the editor and driven by Rust.

**A detour: panels from a function call.** The ray tracer's panel was a class whose only job was to call one method back. The editor now takes a path and a function: `editor->AddPanel("Debug/Ray Tracer", [this] { DrawRayTracerUI(); })`. The path's segments become submenus under the owner's menu (`Game` → `Debug` → `Ray Tracer`), its last segment the window's title, the whole path its ImGui ID (`Ray Tracer###Debug/Ray Tracer`), so two panels titled the same in different submenus stay two windows. The first version made a layer per panel, and the engine's `LayerOwner` refuses a second layer of the same type (a guard that had caught real bugs before), so the panels aren't layers: one `EditorFunctionPanels` layer holds and draws them all. A detail of ImGui's menus worth knowing: `BeginMenu` returns false for a closed menu, and a closed menu takes no `EndMenu`; the bar closes only the submenus that opened.

**Spheres as entities, step by step (Task 2.3).** Seven small steps, each tested on its own:

1. *A `Vec3` field type* for component schemas: C layout (12 bytes, 4-byte aligned), `gram_core::Vec3` in Rust, `glm::vec3` in C++, a `DragFloat3` in the inspector. A unit test pinned the layout; an edited value came back after Stop, thanks to the `World` snapshot.
2. *`Sphere` and `Material` components*, two `.ron` schemas. Two components rather than one: a material will outlive spheres when cubes or meshes arrive.
3. *Typed C++ access*: the generated structs carry their `ID` and `Descriptor()`, so `World` offers `Get<T>`, `Add<T>` and `Each<T>`. The component storage explained along the way: per type, one packed byte array, an `Owners` array (who owns slot i) and a `SlotOf` map (where entity e's component is); removing swaps the last one into the hole, so the array never has gaps but its order isn't the entities' order.
4. *The ray tracer reads the `World`*: `PackScene` rebuilds the GPU's arrays from the entities every frame and compares them byte for byte with the last frame's, resetting the accumulation only on a real change. That one comparison replaced every "the UI says it changed" flag: inspector edits, Play/Stop and Rust all reset it for free. The trap: destroying entities inside `Each` moves the storage being walked, so the book's scene now deletes in two passes. And one false alarm: a "ghost" sphere while playing turned out to be a `Game` binary built from an intermediate step, before the comparison existed; Play rebuilds only the Rust library, never the C++.
5. *The host learns to iterate*: `GetComponentCount`/`GetComponentAt` on both sides of the `HostAPI`, version 4, and the size asserts the contract had promised but never had.
6. *`Query::iter()`/`iter_mut()` in Rust*: lazy iterators, one borrow guard per element, alive as long as the loop's variable. The lifetime error on the way taught one Rust rule: a `&'a T` needs `T: 'a`. Components hold no references, so `trait Component: 'static` says it once for all of them.
7. *A system moving the spheres*: `bob.rs`, every small sphere bobbing while playing. It adds `A·cos(t + phase)·dt` per frame instead of setting `y = y0 + A·sin(t)`, because the component only knows where the sphere is now, not where it started. Pause lets the accumulation clean the image; Stop puts every sphere back.

**Describing a shader once (Task 2.3a).** Every pass so far repeated itself: a uniform struct in HLSL and the same one in C++ with hand-placed padding, the binding numbers on both sides, a CMake line, the shader's name as a string. The layouts were where bugs kept appearing. Now a `.pass.ron` sits next to each shader:

```ron
Pass(
    name: "DepthOfField",
    stage: Raster,
    uniform: [ Field(name: "texel_size", ty: Vec2), Field(name: "focus_distance", ty: F32), ... ],
    inputs:  [ Sampled(name: "color"), Sampled(name: "depth", ty: F32) ],
)
```

and `gram build` writes the rest: `Passes.generated.hpp` (`DepthOfFieldParams` with size and offset asserts, the shader's name, a binding constant per resource), `DepthOfField.generated.hlsli` next to the shader (the same struct, offsets as comments, every `[[vk::binding]]`), and `Shaders.generated.cmake` (the line compiling it). The ray tracer's file adds structured buffers (`SphereData`, `MaterialData`) and storage outputs; `Default.hlsl` brought a `Mat4` type.

The heart of it is one function, `layout`, with one rule stricter than any GPU's: a `vec3`/`vec4`/matrix starts on a 16-byte boundary, a `vec2` on 8, a scalar on 4, and the size rounds up to 16, with every gap written as a named `_PadN` field on both sides. Explicit padding means no compiler's layout rules (std140, std430, dxc's relaxed ones) can make the two sides disagree. One detail: padding in HLSL is written as separate `uint`s, never as an array, because a cbuffer array element takes a whole 16-byte register. Bindings follow one convention instead of being configured: set 0 holds the uniform then the buffers, set 1 the sampled inputs then the storage outputs. The first phase (the model, the layout, the uniform's two emitters) was written by hand and was the hard part; inputs, buffers, compute passes and the CMake file were the same loop with more cases.

What it bought: adding a pass is a `.pass.ron` and a shader; the C++ class still has to be written, but it starts from a header that says exactly what the shader needs. Next idea (Task 2.3b): generate that class's boilerplate too.

## Part 5 — A BVH, and where the time really goes (Nov 19)

The book's scene ran at about 8 FPS, and the obvious suspect was the search: every ray, at every bounce, tested all 486 spheres. A bounding volume hierarchy (BVH) is the textbook answer. Spec first (`docs/wave-2-bvh.md`), then four pieces, each checked before the next.

**The build (C++).** A tree of boxes over the spheres: put a box around all of them, cut the group in two, box each half, repeat until a group holds one or two spheres. The insight that unblocked it: you don't cut *boxes*, you cut the *list of spheres* (with `std::partition`, quicksort's partitioning step) and box each half afterwards. Nodes are worked on by index, two children always appended next to each other, so one index (`First`) finds both. A bug on the way: the "longest axis" was a chained ternary that compared Z with Y instead of with the longest so far, so some nodes were cut along their short side. The tree was still valid, only worse.

**The validator, before any shader.** A second walk over the tree that checks it and counts it, shown in the Ray Tracer panel: every sphere in exactly one leaf, every box inside its parent's, every child index real, plus nodes, leaves, depth, fullest leaf and build time. One invariant does most of the work: a binary tree where every interior node has two children has `nodes = 2 · leaves − 1` (579 = 2 · 290 − 1 on the book's scene). The validator had its own bug (a leaf's loop ran to `Count` instead of `First + Count`, so it checked nothing) and a test of its own: break the build on purpose and see it say "invalid". Building takes about 0.45 ms, so it simply rebuilds whenever the scene changes.

**The traversal (HLSL).** No recursion on the GPU: a fixed stack of 32 node indices, the root pushed first; pop a node, skip it if the ray misses its box, test its spheres if it's a leaf, push its children otherwise. The ray–box test is the slab test: per axis, when the ray crosses the two planes; the ray is inside the box from the last entry to the first exit. Two typos there (a `min` for a `max`, a `max` for a `min`) made almost every box look hit: the image stayed right and the frame got slower, which is exactly how that bug looks. The search range shrinks to the closest hit found so far, so boxes behind it are skipped.

**Spheres vanishing: an order bug.** Building the tree reorders the spheres (each leaf's spheres contiguous), and the comparison that decides whether the scene changed must use the order they came from the `World` in, or a still scene looks changed every frame. The first fix kept that order for the comparison but went on replacing the uploaded spheres every frame, so on every frame after a rebuild the nodes pointed at spheres in a different order than they were built for. Only the BVH path showed it, since the plain loop tests everything anyway.

**The ground.** The ground is a sphere of radius 1000; whichever node holds it gets a box 2000 units wide, and so does every node on the way down to it. Huge spheres (radius over 10) now go first in the array and stay out of the tree; the shader tests them one by one before walking it, which also shrinks the search range early, since most bounces end on the ground.

**Seeing the algorithm: a heat map.** A counter of box and sphere tests for the camera's ray, drawn as color (blue cheap, red expensive) behind a Show cost checkbox. Without the BVH the whole screen is red, the sky included: a ray that hits nothing still tests 486 spheres to find that out. With it, the sky is blue (one box test: the root's) and only the clusters of spheres turn pink. It's the picture that makes the algorithm visible.

**Where the time really went.** At twice the resolution with 10 bounces, the BVH took a frame from 190 ms to 77 ms (5.3 → 12.9 FPS), then stopped helping: removing the ground from the tree changed nothing measurable, and the heat map was already blue. The search was no longer the cost; pixels and bounces were. A Quality panel made those settings adjustable (SSAA 1x / 2x / 4x, samples per frame, max bounces): at 1x SSAA and 4 bounces the same scene runs at 86 FPS. And SSAA turned out to be nearly redundant here: every ray is already jittered inside its pixel, so the accumulation's average over frames *is* supersampling, spread over time; SSAA only helps while the scene moves and the accumulation keeps restarting. The defaults became 1x SSAA, 6 bounces, 1 sample.

**Better trees: SAH, and the nearer child first.** Two improvements from the literature. The Surface Area Heuristic picks a cut by price rather than at the middle: the chance that a ray enters a box is proportional to its surface area (not its volume), so a child costs its area times its sphere count, and the cheapest of 7 candidate cuts per axis wins, or no cut at all when testing everything is cheaper. And the traversal tests both children before pushing them and visits the nearer first, so a hit there often makes the farther one skippable. A bug worth telling: when SAH found no cut worth making, an empty `if` let it partition anyway along axis −1. Measured at 1x SSAA, 6 bounces, 1 sample:

| | FPS | Nodes | Depth | Fullest leaf |
|---|---|---|---|---|
| no BVH | 24 | – | – | – |
| midpoint | 46 | 577 | 9 | 2 |
| SAH | 49 | 563 | 9 | 2 |

The BVH doubles the frame rate at these settings; SAH adds a little on a scene this regular (a grid of similar spheres, where the middle is already a good cut). It should matter more on uneven geometry: a triangle mesh is the real test.

**Next.** One triangle in the ray tracer (the textured quad's two), then an `.obj` loader with the same BVH over triangles. A rotating model won't need its tree rebuilt: built once in the object's own space, it stays valid while the ray is moved into that space with the object's inverse transform. That's the two-level split hardware ray tracing uses (a bottom-level tree per model, a small top-level one over the instances).

## Lessons worth a paragraph each

- **The validation layer is the tutor.** Almost every Vulkan step was a validation error first; reading them carefully was faster than any guess.
- **Layouts are a contract.** Every struct crossing to a shader has a size assert on the C++ side; repacking them (materials 48 → 32 bytes) is safe only because of those asserts.
- **Names carry units.** `FocalLength` holding a distance in metres cost an evening; `FocusDistance`, `LensRadius`, `SampleScale` are names that say what's in them.
- **Know what the hardware already does.** The sRGB swapchain applies gamma; pipeline blending computes a running average; a sampler clamps at the edges. Doing it again by hand is a bug.
- **Physically based is about the material and the light, not about rays.** A rasterizer can be PBR; what a ray tracer adds is knowing where light comes from.
- **Data in one place.** The ray tracer got simpler the day it stopped owning its spheres: the editor, Play/Stop and Rust all came for free once the scene lived in the `World`. The same move, one level down, gave the pass generator: a struct described once can't disagree with itself.
- **Detect changes, don't announce them.** Comparing this frame's packed data with the last one replaced every "something changed" flag, and stays right when a new source of changes appears.
- **Measure before optimising the next thing.** The BVH was the right fix for the search, and the heat map proved the search was solved; the frame was still slow because the cost had moved to pixels and bounces. Without a number per change, the ground's removal would have looked like a win.
- **Validate data before drawing it.** The tree's validator found its own bugs and stayed silent on the real ones, which were on the GPU side; knowing the tree was right narrowed the search to the shader and the upload.

## Screenshots worth taking for the post

The quad with the photo (end of part 1). The first noisy diffuse sphere next to the same one after a few hundred accumulated frames. Lambertian/metal/glass side by side. The hollow glass sphere with its rings, at 4 and at 12 bounces. Depth of field through the lens next to the post-process at the same settings, with a sphere in the foreground. The book's cover scene, with the FPS counter in the menu bar. The book's scene with the spheres bobbing (a short clip), the Outliner listing ~487 entities and one selected in the Inspector. A `.pass.ron` next to the two files generated from it. The heat map without and with the BVH (red screen, then blue sky and pink clusters). The Ray Tracer panel with the tree's stats and the Midpoint/SAH combo.
