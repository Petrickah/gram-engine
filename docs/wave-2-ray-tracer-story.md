# Wave 2 — the ray tracer, the story so far

A chronological account of 2026-11-05 → 2026-11-13: from a triangle with no buffers to *Ray Tracing in One Weekend* running in a compute shader, up to the book's cover scene, with progressive accumulation, supersampling and depth of field done twice (a real lens, then a post-process). It exists to keep the sequence straight for the Episode 2 devlog: what was tried, what broke, what each bug taught. The technical reference for the renderer as built is `docs/wave-2-renderer-guide.md`; this is the narrative.

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

## Lessons worth a paragraph each

- **The validation layer is the tutor.** Almost every Vulkan step was a validation error first; reading them carefully was faster than any guess.
- **Layouts are a contract.** Every struct crossing to a shader has a size assert on the C++ side; repacking them (materials 48 → 32 bytes) is safe only because of those asserts.
- **Names carry units.** `FocalLength` holding a distance in metres cost an evening; `FocusDistance`, `LensRadius`, `SampleScale` are names that say what's in them.
- **Know what the hardware already does.** The sRGB swapchain applies gamma; pipeline blending computes a running average; a sampler clamps at the edges. Doing it again by hand is a bug.
- **Physically based is about the material and the light, not about rays.** A rasterizer can be PBR; what a ray tracer adds is knowing where light comes from.

## Screenshots worth taking for the post

The quad with the photo (end of part 1). The first noisy diffuse sphere next to the same one after a few hundred accumulated frames. Lambertian/metal/glass side by side. The hollow glass sphere with its rings, at 4 and at 12 bounces. Depth of field through the lens next to the post-process at the same settings, with a sphere in the foreground. The book's cover scene, with the FPS counter in the menu bar.
