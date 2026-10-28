# Notes — drawing the Sphere entity (Task 1.4 → Wave 2)

Status as of 2026-10-19: **notes for future work, nothing implemented.** Update 2026-10-28: the sphere moved entirely to Wave 2 (Task 2.1). Wave 1's Task 1.4 is now a text component edited in ImGui (`wave-1-rust-gameplay-design.md`), so the "Task 1.4" section below is no longer planned; the Wave 2 notes stand. The `gram-core` / `cxx` mentions below predate the adopted design (state in C++, Rust as a stateless `cdylib`). Written so the reasoning doesn't have to be rediscovered when Task 1.4 and Wave 2 (Task 2.1) start. Some statements about third-party support (MoltenVK, GPU vendors) come from memory — re-check them against current docs before depending on them.

## The idea in one line

Entities are spheres (position / color / radius, owned by Rust). The renderer draws a **screen-sized surface with a ray tracer behind it**: no mesh is rasterized; each pixel casts a ray and tests it against the spheres. The sphere data travels to the shader; the shader does the rest.

Task 1.4's goal is to prove the data flow (entity in `gram-core` → system in `gram-hot` changes it → C++ draws the new value, live, without a restart), **not** to build the renderer. Keep its drawing as small as possible; the real renderer is Wave 2.

## Task 1.4 — the smallest drawing that proves the flow

- **Fullscreen triangle**, not a two-triangle quad. One triangle larger than the screen covers it entirely. The vertex shader already generates positions from `SV_VertexID` without a vertex buffer (as `Default.hlsl` does for the current triangle), so this is just three different positions — and `draw(3, 1, 0, 0)` stays as is.
- **Fragment shader**: build a ray per pixel from the pixel position (and an aspect ratio from the extent), intersect it with one sphere, shade by color/normal, or output the background.
- **Data via push constants**: position (3 floats), radius, color — a handful of floats. The Vulkan spec guarantees at least 128 bytes of push constants. This needs only a `VkPushConstantRange` in the (currently empty) pipeline layout and `cmd.pushConstants(...)` per frame. No buffers, no memory allocation, no descriptors.
- The C++ side reads the sphere from the `World` (via the `cxx` bridge to `gram-core`) each frame and pushes it. Hot reload then only changes *what values arrive*, which is exactly what 1.4 wants to demonstrate.

## Wave 2 — many spheres

### Data: storage buffer, not uniform buffer
- A **UBO** needs a fixed-size array in the shader, and the guaranteed maximum size is small (16 KB). A **storage buffer (SSBO)** can be sized at runtime — right for a variable number of spheres.
- This is the first time the renderer needs **buffer memory**: pick a memory type (host-visible for per-frame uploads), allocate, map. Writing this by hand once is educational; afterwards `VulkanMemoryAllocator` (VMA, available through vcpkg) is the usual choice.
- Also first needed: a **descriptor set layout**, a **descriptor pool**, a **descriptor set**, and the layout added to the pipeline layout.
- **Per frame in flight**: the CPU writes the sphere buffer while the GPU may still read the previous frame's. One buffer (and descriptor set) per frame in flight, indexed by the frame index. This is why *Frames in flight* (rest of Task 1.2) should be finished first.
- Layout: SSBO structs follow std430-like alignment rules; a `float3` followed by a `float` packs into 16 bytes, which fits `{position, radius}` and `{color, padding}`. Mirror the struct on the C++/Rust side with `#[repr(C)]` / `alignas` and check sizes with `static_assert`.

### Compute shader (ported from the Unity article)
- A compute shader writes into a **storage image** (`image2D`), one thread per pixel (workgroup e.g. 8×8, dispatch `ceil(extent / 8)`).
- **Don't assume the swapchain image can be a storage image.** `imageUsage = eStorage` on the swapchain is not guaranteed on every platform/driver, and on macOS through MoltenVK it is likely not available. Safe plan: render into an **own image** (usage `eStorage | eTransferSrc | eSampled`), then either `blit`/`copy` it to the swapchain image, or draw a fullscreen triangle that samples it. The own image has to be recreated on resize (same pattern as the swapchain).
- The same own image is what an ImGui scene viewport needs (see `wave-1-imgui-plan.md`, step 6), so building it once serves both.
- Needs a compute pipeline (`createComputePipelines`), image layout transitions `General` ↔ `TransferSrc` etc., and a compute dispatch in the command buffer instead of (or before) the graphics draw. HLSL compiles the same way with `dxc -spirv -T cs_6_0 -E CSMain`; extend the `gram_add_hlsl_shader` CMake function (today it only produces `.vert.spv`/`.frag.spv`).

### Hardware ray tracing (stretch goal)
- **Not NVIDIA-only.** The Khronos extensions `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline` and `VK_KHR_ray_query` are cross-vendor: NVIDIA RTX (20 series and later), AMD RDNA2 and later (RX 6000+, including Linux with RADV), and Intel Arc.
- **macOS: likely unavailable.** To my knowledge MoltenVK does not expose the Vulkan ray tracing extensions, so the compute path has to stay as the fallback on Mac regardless. Verify before planning around it.
- **Spheres are an awkward fit.** The hardware BVH is built for triangles. Analytic spheres need **procedural AABB geometry with an intersection shader**, which loses much of the benefit. Hardware RT pays off more once the scene has triangle meshes.
- **Device selection**: treat RT as an *optional capability* (enable the extensions and features only if the chosen device offers them, with the compute path otherwise) — not a rejection rule like `geometryShader` is in `RatePhysicalDeviceSuitability`, or the Mac and older GPUs would be filtered out.

## Suggested order

1. Task 1.4: fullscreen triangle + push constants, one sphere.
2. Finish *Frames in flight*, then buffers + descriptors (SSBO of spheres) with the fragment-shader tracer.
3. Task 2.1: move the tracing to a compute shader + own storage image.
4. Optional: hardware RT behind a capability check, once there's a reason (triangle meshes).
