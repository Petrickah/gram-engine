# Wave 1 — Hello Triangle (C++ / Vulkan)

Status as of 2026-10-19: a colored triangle renders on Linux (incl. WSL2), Windows and macOS (MoltenVK). The window can be resized, minimized and maximized without errors, and closes cleanly. Still open in Task 1.2: *frames in flight* (see the last section). Tasks 1.3+ (the Rust split) haven't started — this doc covers only the C++ side, and feeds the Episode 1 devlog (Task 1.5).

Everything below lives in `GramEngine/`, a `cmake` module of the project (`Gram.ron`: deps `vulkan`, `glfw3`, `glm`, via vcpkg).

## The big picture

```
main.cpp ──CreateApplication()──► GLFWApplication (window, event loop)
                                       │ owns
                                       ▼
                                 VulkanRenderer ── composed of ──►  VulkanInstance
                                  (DrawFrame)                       VulkanSurface
                                                                    VulkanDevice
                                                                    VulkanSwapchain
                                                                    VulkanPipeline ── VulkanShader ×2
                                                                    VulkanFrame
```

Two abstraction layers sit between the program and Vulkan, on purpose: the engine shouldn't hard-code "GLFW" or "Vulkan" everywhere, because the renderer may be swapped later (and `Application` is where the Rust game layer will eventually plug in).

- `Application` (`include/Application.hpp`) — owns the window-system side and a `Renderer*`. Pure virtual `Run()`.
- `Renderer` (`include/Renderer.hpp`) — pure virtual `DrawFrame()`, `Finish()`, and a default no-op `OnResize()`.
- `GLFWApplication` / `VulkanRenderer` are the only concrete implementations today.

## Entry point and application layer

### `Gram/EntryPoint.hpp`
Including this header turns the translation unit into the program's entry point. It defines `main()` on Linux/macOS and `WinMain()` on Windows. `WinMain` reads `__argc`/`__argv`, set up by the CRT, so both entry points hand `CreateApplication(argc, argv)` the same arguments. The user-side code (here `main.cpp`) must define `Gram::CreateApplication`; the entry point calls it, then `app->Run()`, then deletes the app.

Lowercase `<windows.h>` on purpose: mingw installs it lowercase and the cross-compile from Linux hits a case-sensitive filesystem.

### `main.cpp` — `CreateApplication`
Builds the configuration, nothing else:

1. `ApplicationSpecification`: window size (1280×720) and title.
2. Lists of requested **instance extensions** (`VK_EXT_debug_utils`, plus `VK_KHR_portability_enumeration` on Apple), **device extensions** (`VK_KHR_swapchain`) and **validation layers** (`VK_LAYER_KHRONOS_validation`).
3. A `VulkanRendererSpecification` carrying those, `m_DisplayDebug = true` (verbose listing of everything Vulkan reports, with `[X]`/`[-]` markers for requested/not requested), the `Application*`, and `m_ProjectWorkspace` — the directory assets are loaded from.
4. The window is created **first** (`new GLFWApplication`) because the renderer needs it: GLFW tells Vulkan which instance extensions the platform's surface needs, and the surface is created from the window.
5. `SetRenderer(new VulkanRenderer(&rendererSpec))` — the application takes ownership.

`ApplicationSpecification::GetRequired*` (in `GLFWApplication.cpp`) merge what GLFW requires (`glfwGetRequiredInstanceExtensions`) with what `main.cpp` asked for, and ask the renderer spec to validate the result.

### `GLFWApplication`
- Constructor: `glfwInitVulkanLoader(vkGetInstanceProcAddr)` (so GLFW uses the same Vulkan loader entry point as the engine), error callback to `std::cerr`, `glfwInit`, `GLFW_NO_API` (no OpenGL context — Vulkan owns the window's surface), resizable window, `glfwCreateWindow`. Registers the framebuffer-size callback (see *Resize*).
- `Run()`: `while (!shouldClose)`: poll events; if the framebuffer is 0×0 (minimized) sleep in `glfwWaitEvents()`; otherwise `DrawFrame()` — a `false` return ends the loop. After the loop, `Finish()` waits for the GPU to be idle so destruction can't race in-flight work.
- Destructor: deletes the renderer **before** destroying the window — the renderer's surface/swapchain depend on it.

## Conventions used throughout the renderer

- **No exceptions** (`VULKAN_HPP_NO_EXCEPTIONS`): errors come back as `vk::Result` and are checked by hand; the enhanced calls (`createShaderModuleUnique`, …) return `vk::ResultValue`. `VULKAN_HPP_NO_CONSTRUCTORS` makes the `vk::*CreateInfo` structs aggregates, so they're written with designated initializers (`{ .stage = ..., .pName = ... }`), as in the Khronos tutorial. Both are global compile definitions — they must match in every translation unit that includes `vulkan.hpp`.
- **Constructors don't throw — they leave the object invalid.** Each component starts with an early-return guard (`if (!rendererSpec || !device) return;`), logs on failure and returns with its handles still null. Destructors only destroy handles that are non-null, so a half-built component tears down safely.
- **`UniqueObject`** (non-copyable, non-movable) marks components that own raw Vulkan handles at a fixed address — copying one would destroy a handle twice. **`MoveObject`** (move-only) is for objects whose members all own themselves (`vk::Unique*`, std containers) — `VulkanShader` is the one user.
- Handles cross component boundaries as plain C handles (`VkDevice`, `VkPipeline`, …) and are cast to `vk::` wrappers at the call site. The C++ `vk::` classes are thin wrappers around the same pointer, so this is free.
- The `[X]` / `[-]` / `[+]` / `[!]` lines in the debug output mean: requested+available / available but not requested / auto-enabled (portability subset) / requested but missing.

## The components, in construction order

`VulkanRenderer`'s members are declared (and therefore constructed) in dependency order: instance → surface → device → swapchain → pipeline → frame. C++ destroys members in the reverse order, which is exactly the order Vulkan needs.

### `VulkanInstance` — the connection to Vulkan
Creates the `vk::Instance`: app/engine info, API version 1.3, the validation layers and extensions — but only if the driver actually offers *all* of them (otherwise `createInstance` fails with `eErrorLayerNotPresent`). With validation on, a **debug messenger** is created: the `VkDebugUtilsMessengerCreateInfoEXT` is chained into the instance create info (so problems in `createInstance`/`destroyInstance` themselves are reported) and again created as a real messenger afterwards. `debugCallback` prints `[Vulkan] validation layer: ...`. The messenger functions aren't linked statically — they're looked up with `vkGetInstanceProcAddr`, as they're extension functions. On Apple the `eEnumeratePortabilityKHR` flag is set, otherwise MoltenVK devices don't show up at all.

`VulkanRendererSpecification::AreExtensionsSupported` / `AreValidationsSupported` are defined here: enumerate what the driver offers, print the table, and return `true` only if **every** requested name is offered.

### `VulkanSurface` — the window as a Vulkan target
One call: `glfwCreateWindowSurface(instance, window, nullptr, &surface)`. GLFW picks the right platform extension (Win32/Xlib/Wayland/Metal). Needs the `Application` to be a `GLFWApplication` (`dynamic_cast`). Destroyed with `vkDestroySurfaceKHR`.

### `VulkanDevice` — physical GPU, queues, logical device
**Choosing the GPU** (`PickPhysicalDevice`): enumerate all physical devices, list each one (name/IDs, queue families, extensions) when debugging, then *score* each:
- tier by device type: discrete +300000, integrated +200000, virtual +100000, anything else +0 — non-overlapping, so a discrete GPU always beats an integrated one;
- plus `maxImageDimension2D` as a tie-breaker inside a tier;
- **rejected outright** (with a logged reason) if: it lacks the requested device extensions; it has no graphics + present queue families; its API version is below 1.3; on non-Apple platforms it lacks `geometryShader`; on Apple it lacks `VK_KHR_portability_subset`. That last rule is deliberate: a Mac can expose the same GPU through two Metal-backed drivers (MoltenVK and KosmicKrisp) with identical properties; MoltenVK is the one that offers the portability subset, so the other is filtered out.

The highest score wins (`std::multimap`, last element).

**Queue families** (`FindQueueFamilies`): looks for the first family with `eGraphics` and the first that can present to the surface (`getSurfaceSupportKHR`). Stored in `QueueFamilyIndices`, a map from name to `optional<uint32_t>` — `IsComplete()` is true when all are set. `TryGetDeviceIndices` is used by the swapchain to decide between exclusive/concurrent image sharing.

**Logical device** (`CreateLogicalDevice`): one queue (priority 1.0) per *unique* family (graphics and present are often the same one — asking twice for the same family is invalid). Enabled via a `pNext` chain: `VkPhysicalDeviceFeatures2` → `VkPhysicalDeviceVulkan13Features { synchronization2, dynamicRendering }`. These two are why the renderer needs no `VkRenderPass`/`VkFramebuffer`: rendering is begun directly on image views (`beginRendering`) and layout transitions use `pipelineBarrier2`. `pEnabledFeatures` must stay null when `Features2` is chained (spec rule). On Apple, `VK_KHR_portability_subset` is appended to the enabled extensions whenever the device offers it (the spec *requires* enabling it in that case, so it can't be a plain request in `main.cpp`, where it would break conformant drivers).

`Submit(...)` submits the command buffer to the graphics queue (signalling the draw fence) and presents. It returns the **present result untouched** — see *Resize and minimize* for why the C function `vkQueuePresentKHR` is called instead of `vk::Queue::presentKHR`.

### `VulkanSwapchain` — the images that go to the screen
A swapchain is a queue of images the GPU draws into and the presentation engine shows. `Build()` does, in order:
1. `QuerySwapchainSupport` — surface capabilities, formats, present modes (queried **every build**, since `currentExtent` changes with the window).
2. **Extent** (`ChooseSwapchainExtent`): if the surface dictates a size (`currentExtent` ≠ `UINT32_MAX`) use it; otherwise take the window's framebuffer size from GLFW, clamped to the min/max extents. (Framebuffer size is in pixels, which on HiDPI/Retina differs from window size in screen coordinates — which is why it's the framebuffer size that's asked for.)
3. **Format**: prefer `B8G8R8A8Srgb` + sRGB-nonlinear, then any sRGB-nonlinear, then the first one offered.
4. **Present mode**: FIFO (vsync) — the only mode the spec guarantees.
5. **Image count**: `minImageCount + 1` (so the driver never makes us wait on it), capped by `maxImageCount` when that's nonzero.
6. `createSwapchainKHR`: color-attachment usage; **exclusive** image sharing if graphics and present are the same family, **concurrent** across the (deduplicated) families otherwise; `clipped = true`; the previous swapchain is passed as `oldSwapchain`.
7. One `vk::ImageView` per image (2D, identity swizzle, color aspect, 1 mip, 1 layer).

The driver may create more images than requested, so validity compares the real image count with the view count (`IsValid()`).

### `VulkanShader` and the shader pipeline
**Authoring:** `shaders/Default.hlsl` holds both stages (`VSMain`, `PSMain`) in HLSL. The vertex shader has no vertex buffer: it indexes hard-coded position/color arrays with `SV_VertexID` (the draw call is just `draw(3, 1, 0, 0)`), which keeps this milestone free of buffer code. The color is passed to the pixel shader through `[[vk::location(0)]]`.

**Compiling:** CMake runs the Vulkan SDK's `dxc -spirv` twice per `.hlsl` (`-T vs_6_0 -E VSMain` → `Default.vert.spv`, `-T ps_6_0 -E PSMain` → `Default.frag.spv`) into `<build dir>/shaders/`, as part of the `GramShaders` target the executable depends on. `dxc` is looked up with a plain `find_program` (hints: `$VULKAN_SDK/bin`) rather than `FindVulkan`'s component, which would also demand the *target's* `dxcompiler` library — impossible for a mingw cross-build. SPIR-V is platform-independent, so the host's `dxc` serves every target.

**Shipping:** `gram build` copies `<build dir>/shaders/*` next to the built executable (`build/<project>/<target>/shaders/`). Stopgap until gram has a dedicated shader module kind.

**Loading:** `VulkanShader` reads `<workspace>/<filename>` as bytes and creates a `vk::UniqueShaderModule`, which destroys itself — hence `MoveObject`.

**Where `<workspace>` comes from:** `GetExecutableDirectory()` (`Gram/Platform.hpp`), asked from the OS: `GetModuleFileNameW` on Windows, `/proc/self/exe` on Linux, `_NSGetExecutablePath` on macOS. `argv[0]` is not reliable: an `.exe` launched from WSL (`gram run --target windows`) gets a *bare* `GramEngine.exe`, so its parent path is empty and every asset path became relative to the current directory — the shaders weren't found, the pipeline was null, and validation complained about `vkCmdBindPipeline(VK_NULL_HANDLE)`.

### `VulkanPipeline` — the fixed recipe for drawing
Built once; since rendering is *dynamic*, nothing in it depends on the swapchain size or images.
- Two shader stages (vertex/`VSMain`, fragment/`PSMain`); the modules are only needed until `createGraphicsPipelines` returns, so they're locals of the constructor.
- Empty vertex input; `TriangleList`; fill; back-face culling with clockwise front faces; no multisampling.
- Color blending enabled (`srcAlpha`/`oneMinusSrcAlpha`), all four channels written.
- **Dynamic state: viewport and scissor** — set per frame in the command buffer. This is what lets a resize avoid rebuilding the pipeline.
- Empty pipeline layout (no descriptors, no push constants yet).
- `PipelineRenderingCreateInfo` chained in with the swapchain's color format — the dynamic-rendering replacement for a render pass, telling the pipeline what it'll render into.

### `VulkanFrame` — recording the commands
One command pool (flag `eResetCommandBuffer`, so `begin()` implicitly resets) and **one** command buffer, on the graphics family. Per frame:
- `StartFrame`: `begin`; transition the acquired image `Undefined → ColorAttachmentOptimal` (a barrier with `synchronization2` structs); `beginRendering` with a color attachment cleared to opaque black (`loadOp = clear`, `storeOp = store`) over the full extent.
- `RenderFrame`: bind the pipeline, set viewport + scissor to the current extent, `draw(3, 1, 0, 0)`.
- `EndFrame`: `endRendering`; transition `ColorAttachmentOptimal → PresentSrcKHR` (the layout presentation requires); `end`.

## The frame loop — `VulkanRenderer::DrawFrame`

Three synchronization objects: a **fence** (CPU↔GPU, created already signaled so the first frame doesn't wait forever), a **present-complete semaphore** (GPU↔GPU: "the image is acquired and safe to write"), and one **render-finished semaphore per swapchain image** ("drawing is done, you may present").

```
if swapchain is stale → Recreate (skip the frame if that fails: minimized)
waitForFences(draw fence)                       // the previous frame's GPU work is done
acquireNextImageKHR(.., presentComplete, ..)    // → imageIndex
resetFences
record: StartFrame / RenderFrame / EndFrame
submit: wait presentComplete @ ColorAttachmentOutput
        signal renderFinished[imageIndex]
        signal draw fence
present: wait renderFinished[imageIndex]
```

Why the render-finished semaphores are per **image**: the presentation engine holds the semaphore until that image has actually been presented, and there's no direct way to know when. Indexing by image index guarantees that a semaphore is only reused when the image it belongs to has come back through `acquire`.

## Resize and minimize (swapchain recreation)

A swapchain is bound to the surface's size at creation. When the window changes, the driver reports the swapchain `eErrorOutOfDateKHR` (unusable) or `eSuboptimalKHR` (still works, no longer a perfect match). These are *normal outcomes*, not failures. Before this was handled, they ended the loop (and on present they tripped a `vulkan.hpp` assert).

Three places set `m_SwapchainStale = true`:
1. **`glfwSetFramebufferSizeCallback`** → `Renderer::OnResize()`. Some platforms don't reliably report out-of-date on resize, so the renderer is told directly instead of relying on the driver.
2. **`acquireNextImageKHR`**: out-of-date → nothing was acquired, so skip the frame (return `true`, the fence is untouched and still signaled); suboptimal → the image is usable, draw it and rebuild afterwards.
3. **Present**: suboptimal or out-of-date → mark stale, keep running.

`DrawFrame` starts with `if (m_SwapchainStale && !RecreateSwapchain()) return true;`. `RecreateSwapchain()` → `VulkanSwapchain::Recreate()`: `waitIdle` (old images may be in flight), re-run `Build()` (new extent, image views destroyed and rebuilt, new swapchain created with the old one as `oldSwapchain`, old one destroyed only after the new one exists), then rebuild the render-finished semaphores (the image count can change, and a failed present may leave one signaled; the device is idle, so they're free to destroy).

A minimized window reports a 0×0 surface, which can't back a swapchain: `Build()` returns `false` *silently* (not an error), `DrawFrame` keeps returning `true`, and `GLFWApplication::Run` sleeps in `glfwWaitEvents()` until the window comes back, which fires the callback and rebuilds.

Presenting uses the **C function `vkQueuePresentKHR`**, not `vk::Queue::presentKHR`: under `VULKAN_HPP_NO_EXCEPTIONS` the wrapper `assert`s on anything but success/suboptimal (`vulkan.hpp`, `resultCheck`), and out-of-date must be handled, not asserted.

## Platform notes

- **Windows** is cross-compiled from Linux with mingw (static runtime), run under `wine` or natively; the vcpkg triplet is `x64-mingw-static`. `vcpkg.json` takes the `vulkan` port on Windows (the loader, `vulkan-1.dll`) and just `vulkan-headers` elsewhere.
- **macOS** goes through MoltenVK: `VK_KHR_portability_enumeration` (instance), `VK_KHR_portability_subset` (device, auto-enabled), the KosmicKrisp filter above, and no `geometryShader` requirement. gram builds macOS targets natively only (no cross toolchain between Linux and macOS in either direction).
- **WSL2**: Linux build runs through WSLg; a Windows `.exe` can be run from WSL with `gram run --target windows`.

## Known gaps / next

- **Frames in flight (rest of Task 1.2).** Today there is exactly one frame in flight: one command buffer, one fence, and a single present-complete semaphore — which is only correct *because* the fence wait at the top of `DrawFrame` serialises frames (the CPU idles while the GPU draws). With `MAX_FRAMES_IN_FLIGHT = 2` the Khronos tutorial's layout is: a command buffer, an in-flight fence **and a present-complete (acquire) semaphore per frame in flight**, while the render-finished semaphores stay **per swapchain image**; the frame index advances modulo `MAX_FRAMES_IN_FLIGHT` after every present.
- Present currently goes through the **graphics** queue; if a GPU ever reports a separate present family, the present queue should be fetched from `m_PresentFamily` instead.
- The pipeline layout is empty and the vertices are hard-coded in the shader — vertex/index buffers and descriptors come with the next Vulkan chapters, and Wave 2's compute pipeline.
- Shader compilation and shipping are CMake + `gram build` stopgaps (see above).
