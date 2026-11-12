# Wave 2 — the renderer as built, and the road to the ray tracer

Status 2026-11-07. Two parts. **Part 1** records what exists (frames, passes, images, the scene target, the viewport) and why it is shaped that way. **Part 2** is the plan from here: three chapters of the Khronos Vulkan tutorial (*Vertex buffers*, *Uniform buffers*, *Texture mapping*), then `RayTracePass`. For each chapter it says what the tutorial does, where that lands in Gram, and the traps where Gram differs from the tutorial.

The user writes the Vulkan code of Part 2 (first time learning it). Claude writes the plumbing (CMake, gram, ImGui glue) and reviews on request. A new session should start by reading this document and `.gram.kanban.md` (Task 2.1).

---

# Part 1 — as built

## One frame, start to end

```
VulkanRenderer::OnDrawFrame
├─ swapchain stale?            → RecreateSwapchain (resize/minimize; a minimized window skips the frame)
├─ m_SceneTarget.ApplyRequestedExtent()   → recreate the scene image if the viewport asked for a new size
├─ VulkanFrame::Begin          → wait fence, acquire image N, reset fence, begin command buffer,
│                                reset swapchain image N's State to eUndefined
├─ every game pass:  pass->Record(cmd, ctx)       (TrianglePass: draws into the scene target)
├─ VulkanImGui::Record         → BeginSwapchainRendering (first → clear), ImGui frame, panels
│                                (ViewportPanel samples the scene target), endRendering
├─ VulkanFrame::End            → clear if nothing drew, swapchain image → ePresentSrcKHR,
│                                end, submit, present, advance the frame index
└─ OnPresented on every pass   → ImGui's platform windows (detached panels)
```

## The pieces

| Class | Owns / does |
|---|---|
| `VulkanFrame` | Command pool, one command buffer + fence + acquire semaphore per frame in flight (2), one "finished" semaphore per swapchain image. `Begin` / `End`. `BeginSwapchainRendering` (first call of a frame clears, later ones load). The stale-swapchain flag. |
| `VulkanFrame::Active` | What one frame hands to its passes: `CommandBuffer`, `FrameIndex`, `ImageIndex`, `SwapchainImage` (a `State&`), `SwapchainRendered`. |
| `VulkanPass` | What `VulkanRenderer::AddPass<T>(args...)` takes: `Record(cmd, ctx)`, optional `OnPresented()`, `Finish()`. `ctx` = `{ Device, ActiveFrame, Swapchain }`. |
| `VulkanImage` | An owned image: `UniqueImage` + `UniqueDeviceMemory` + `UniqueImageView` + `UniqueSampler`, extent, format. `Create(device, extent, format, usage)` (device-local memory via `VulkanDevice::FindMemoryType`). `BeginRendering(cmd, loadOp)`. |
| `VulkanImage::State` | An image's last use: `Layout`, `Stage`, `Access` (+ raw `Image`/`View`). Owns nothing, so the swapchain keeps one per image too. `Transition(cmd, layout, stage, access)` emits the barrier from the last use to the next, and skips it only for read-after-read in the same layout. |
| `VulkanRenderTarget` | The scene image (renderer's `GetSceneTarget()`: `eR32G32B32A32Sfloat`, starts at 1280×720). `RequestExtent` (from the viewport, while drawing UI), `ApplyRequestedExtent` (next frame, before passes: `waitIdle`, recreate, `Generation++`). |
| `VulkanPipeline` | `Graphics(device, shader, setLayouts, colorFormat)` / `Compute(device, shader, setLayouts)`; keeps its bind point, so `Bind` / `BindDescriptorSets` need no argument for it. Viewport/scissor are dynamic. |
| `VulkanShader` | `Graphics(workspace, name, device)` (`VSMain`/`PSMain` → `<name>.vert/.frag.spv`) / `Compute(...)` (`CSMain` → `<name>.compute.spv`). Modules are `Unique`, alive until the pipeline is built. |
| `VulkanImGui` | ImGui context + backends, itself a pass (always last, not in `m_Passes`), and the `LayerOwner` of the editor panels. |
| `ViewportPanel` (editor) | Shows the scene target as an ImGui texture, asks it for the panel's size, rebuilds the texture when `Generation` changes. |

Game side: `Game/Runtime/src/TrianglePass.*` (draws into the scene target with the target's extent and format), registered in `RuntimeApplication::RegisterPasses`. The editor gets the scene target (`Editor(ui, world, loader, &sceneTarget)`) and adds the Viewport itself.

Assets: `gram_add_hlsl_raster_shader` / `gram_add_hlsl_compute_shader` (`GramShaders.cmake`) and `gram_add_texture` (`GramTextures.cmake`) put files under `<build dir>/shaders/` and `<build dir>/textures/`; gram copies both next to the executable. The game opens them from `spec.m_Workspace / "shaders" | "textures"`.

## Rules the code relies on

- **One queue family** for graphics + compute (`QueueFamilyIndices::m_GraphicsComputeFamily`), preferred when it also presents. Ordering between passes is barriers inside one command buffer, not semaphores.
- **Every use of an image goes through its `State`.** A pass says how it will use the image next; `Transition` knows how it was used last. Swapchain images restart at `eUndefined` each frame (their contents don't matter) with `Stage = eColorAttachmentOutput`, the stage the acquire semaphore is waited at, so the first barrier of the frame waits for the acquire.
- **Owned images keep their State across frames.** The scene image ends a frame in `eShaderReadOnlyOptimal`; the next frame's first transition waits for the previous frame's sampling. So one image is enough with two frames in flight.
- **No barrier inside a render pass.** That's why a producer leaves its image ready for the consumer (TrianglePass → shader read), since ImGui samples it inside its own rendering.
- **Resize = request now, apply next frame**, after `waitIdle`. Anything holding the image's view (ImGui texture, descriptor sets) compares `GetGeneration()` and rebuilds.
- **Who owns, closes.** The renderer owns the passes (`unique_ptr`) and the ImGui object. A game keeps a raw `TrianglePass*` from `AddPass` to configure it; never a copy.
- **Destruction order**: `OnFinish` waits for the device, then passes and ImGui finish (ImGui textures are removed in panel `OnStop`, before `ImGui_ImplVulkan_Shutdown`); members are destroyed after, passes first (declared last), device last.

## Along the way (mistakes that taught something)

- A `std::map` element is `pair<const Key, T>`; iterating with `const std::pair<Key, T>&` copies each element into a temporary, so `pName` pointed into a dead string.
- Two submits waiting on one binary semaphore: the second waits forever → GPU timeout → device lost.
- Reading a map before inserting into it → `VK_NULL_HANDLE` shader modules.
- A user-declared destructor silently removes the implicit move constructor.
- `return std::move(prvalue)` defeats C++17's guaranteed elision.
- `Active::FrameIndex` advanced instead of `m_FrameIndex`: always frame 0, no frames in flight.
- Viewport set to the swapchain's extent while rendering into a 1280×720 image: a clipped triangle.
- `STB_IMAGE_IMPLEMENTATION` reached twice in one translation unit: stb's implementation has no include guard.

---

# Part 2 — the plan

Order: **Vertex buffers → Uniform buffers → Texture mapping → RayTracePass**. None of the first three is strictly required for the ray tracer, but each teaches something it needs: the spheres reach the compute shader in a **storage buffer** (buffer creation, memory, upload: chapter 1), with camera/time/frame data per frame and descriptors (chapter 2), and the texture chapter puts buffer, image and descriptor together (chapter 3). After them, `RayTracePass` is one descriptor set and a dispatch.

## Step 0 — two helpers the tutorial writes inline

Build these first; every chapter uses them.

### `VulkanBuffer` (like `VulkanImage`)

```cpp
class VulkanBuffer : private MoveObject
{
public:
    static VulkanBuffer Create(VulkanDevice& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

    void*          Map();            // host-visible only; keep it mapped (persistent mapping) for per-frame data
    vk::Buffer     Get()  const;
    vk::DeviceSize Size() const;

private:
    vk::UniqueDeviceMemory m_Memory;   // declared before the buffer: destroyed after it
    vk::UniqueBuffer       m_Buffer;
    vk::DeviceSize         m_Size = 0;
};
```

- Steps: `createBufferUnique` → `getBufferMemoryRequirements` → `FindMemoryType(bits, properties)` → `allocateMemoryUnique` → `bindBufferMemory`. Same as `VulkanImage::Create`, minus the view.
- Properties: **staging / per-frame data** = `eHostVisible | eHostCoherent` (CPU writes, no flush needed); **GPU-only data** = `eDeviceLocal`, filled from a staging buffer.

### One-time commands (the tutorial's `beginSingleTimeCommands`)

A helper in `VulkanDevice` with its own command pool (`eTransient`), never the frames' command buffers:

```cpp
// Records `record` into a fresh command buffer, submits it, and waits for the queue to finish.
bool VulkanDevice::Submit(std::function<void(vk::CommandBuffer)> record);
```

Uploads happen at load time, so waiting (`queue.waitIdle()` or a fence) is fine. Name it something other than the existing `Submit(fence, submitInfo, presentInfo)` if the overload reads confusingly, e.g. `SubmitAndWait`.

## Step 1 — Vertex buffers (chapters *Vertex input description*, *Vertex buffer creation*, *Staging buffer*)

**Goal:** the triangle's positions and colors come from the CPU instead of `SV_VertexID`.

- **Vertex struct + descriptions.** `struct Vertex { glm::vec2 Position; glm::vec3 Color; }` with a `vk::VertexInputBindingDescription` (stride `sizeof(Vertex)`, `eVertex` rate) and two `vk::VertexInputAttributeDescription` (`eR32G32Sfloat` at `offsetof(Vertex, Position)`, `eR32G32B32Sfloat` at `offsetof(Vertex, Color)`).
- **Gram difference: the pipeline takes them as parameters.** `VulkanPipeline::Graphics` has an empty `vk::PipelineVertexInputStateCreateInfo`. Add `std::span<const vk::VertexInputBindingDescription>` and `std::span<const vk::VertexInputAttributeDescription>` parameters (empty = no vertex input, so a fullscreen pass still works).
- **Shader (HLSL through dxc):** inputs need explicit locations matching the attributes:
  ```hlsl
  struct VSInput { [[vk::location(0)]] float2 Position : POSITION; [[vk::location(1)]] float3 Color : COLOR; };
  ```
- **Staging:** create a host-visible staging buffer, `Map` + `memcpy`, create a device-local buffer with `eVertexBuffer | eTransferDst`, then `SubmitAndWait([&](cmd) { cmd.copyBuffer(staging, vertex, region); })`. The staging buffer can die right after.
- **Draw:** `cmd.bindVertexBuffers(0, { buffer }, { 0 })` before `draw(3, 1, 0, 0)`. The pass owns the buffer (a member), created in its constructor.
- *Index buffer* chapter: skip unless you want a quad.

**Done when:** the same triangle, now from a vertex buffer, in the viewport.

## Step 2 — Uniform buffers (chapters *Descriptor layout and buffer*, *Descriptor pool and sets*)

**Goal:** per-frame data (e.g. `Time`, or a rotation matrix) reaches the vertex shader; this is where descriptors are learned.

- **One buffer per frame in flight**, indexed by `ctx.ActiveFrame.FrameIndex`. The CPU writes frame N+1's buffer while the GPU may still read frame N's. Same for the descriptor sets: one per frame in flight, each pointing at its frame's buffer. (`Gram::vkMaxFramesInFlight` is the count.)
- **Persistent mapping:** map each uniform buffer once at creation and write through the pointer every frame (`eHostVisible | eHostCoherent`, no flush).
- **Descriptor set layout:** binding 0, `eUniformBuffer`, stage `eVertex`. Pass it to `VulkanPipeline::Graphics(..., setLayouts, ...)`: the parameter already exists.
- **Descriptor pool:** `maxSets = vkMaxFramesInFlight`, pool size `eUniformBuffer × vkMaxFramesInFlight`. One pool per pass is fine for now.
- **Write the sets** once (`updateDescriptorSets` with a `vk::DescriptorBufferInfo` per frame); in `Record`: write the frame's data, `m_Pipeline.BindDescriptorSets(cmd, { m_Sets[ctx.ActiveFrame.FrameIndex] })`, draw.
- **Shader:**
  ```hlsl
  struct FrameData { float4x4 Model; float Time; };
  [[vk::binding(0, 0)]] ConstantBuffer<FrameData> frame;
  ```
- **Layout traps (std140 / HLSL cbuffer packing):** members are packed into 16-byte rows; a `float3` followed by a `float` shares a row, but a `float3` followed by another `float3` does not. Mirror the struct in C++ with `alignas(16)` where needed and `static_assert(sizeof(...) == ...)`. GLM matrices are column-major like SPIR-V's default; dxc's HLSL default is also column-major for `float4x4` in buffers, but the tutorial's `perspective` flips Y for OpenGL: multiply `proj[1][1] *= -1`.
- **Simpler alternative worth knowing:** push constants (`vk::PushConstantRange` in the pipeline layout + `cmd.pushConstants`) carry up to 128 bytes per draw with no buffer or descriptor. Good for the ray tracer's frame index later; the tutorial doesn't cover them.

**Done when:** the triangle rotates (or pulses) from a CPU-side time value, with two frames in flight and no validation errors.

## Step 3 — Texture mapping (chapters *Images*, *Image view and sampler*, *Combined image sampler*)

**Goal:** `textures/texture.jpg` on the triangle. Loading with stb already exists (`TrianglePass::LoadTexture`).

- **Upload:** staging buffer with the pixels → `stbi_image_free` right after the copy (not in the pass's destructor) → a `VulkanImage` with `eTransferDst | eSampled`, format **`eR8G8B8A8Srgb`** (a photo is sRGB-encoded; `Unorm` would show it too dark after the sRGB swapchain or too bright in the viewport).
- **Transitions with `State`, in one `SubmitAndWait`:**
  ```cpp
  image.Transition(cmd, eTransferDstOptimal, eTransfer (or eCopy), eTransferWrite);
  cmd.copyBufferToImage(staging, image, eTransferDstOptimal, region);
  image.Transition(cmd, eShaderReadOnlyOptimal, eFragmentShader, eShaderSampledRead);
  ```
  The tutorial's `transitionImageLayout` with its `if/else` on layouts is exactly what `State::Transition` replaces.
- **`VulkanImage::Create` creates a sampler for every image.** Fine for now; when a second image needs one, move the sampler out (one sampler serves many images).
- **Descriptor:** binding 1, `eCombinedImageSampler`, stage `eFragment`, next to the uniform buffer. Pool size grows by `eCombinedImageSampler × vkMaxFramesInFlight` (or one set just for the texture, since it doesn't change per frame).
- **HLSL trap:** in HLSL a texture and a sampler are two objects. Through dxc they become **two separate descriptors** unless both are marked as one combined image sampler on the same binding:
  ```hlsl
  [[vk::combinedImageSampler]] [[vk::binding(1, 0)]] Texture2D    albedo;
  [[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerState albedoSampler;
  // albedo.Sample(albedoSampler, uv)
  ```
  Without the attribute, declare binding 1 as `eSampledImage` and binding 2 as `eSampler` instead.
- **UVs:** a third vertex attribute (`glm::vec2 TexCoord`, location 2), as the tutorial does. Without a vertex buffer they can come from `SV_VertexID` like the positions.

**Done when:** the photo shows on the triangle in the viewport.

## Step 4 — `RayTracePass`

`Game/Content/shaders/RayTracer.hlsl` is ready (`rgba32f` storage image at binding 0, set 0, sized from `GetDimensions`). Remaining:

1. **The scene target** (`VulkanRenderer` constructor): format **`eR32G32B32A32Sfloat`**, usage `eColorAttachment | eSampled | eStorage`. Vulkan guarantees this format as storage image and color attachment, but not as a linearly filterable sampled image (fine on the M4 Pro; `R16G16B16A16Sfloat` is the portable choice if a device refuses it); `rgba8` isn't guaranteed as storage. (Claude can do this change.)
2. **Descriptors:** a layout with binding 0 `eStorageImage` for `eCompute`; a pool with one set; `VulkanPipeline::Compute(device, VulkanShader::Compute(workspace, "RayTracer", device), { layout })`.
3. **Descriptor contents follow the image:** write `{ view, imageLayout = eGeneral }`. In `Record`, if `m_Target.GetGeneration()` changed since the last write, `updateDescriptorSets` again. Safe without per-frame sets: the target waited for the device before recreating.
4. **Record:**
   ```cpp
   Gram::VulkanImage& scene = m_Target.GetImage();
   scene.Transition(cmd, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);
   m_Pipeline.Bind(cmd);
   m_Pipeline.BindDescriptorSets(cmd, { m_Set });
   const vk::Extent2D extent = scene.GetExtent();
   cmd.dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);    // 8×8 groups, rounded up
   scene.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
   ```
5. **Game:** `AddPass<RayTracePass>(...)` instead of `TrianglePass` in `RegisterPasses`.

**Done when:** the gradient fills the viewport at any panel size.

## After that (Task 2.1 proper)

- **Spheres in an SSBO** (`eStorageBuffer`, host-visible, one per frame in flight), struct packed in 16-byte rows: `{ float3 position; float radius; float3 color; float pad; }`, mirrored in C++ with a `static_assert`. First owned by C++, then filled from Rust entities (a schema-defined component; `Query::iter` + a new `HostAPI` function, `GameAPIVersion` 4, see the kanban). Notes: `wave-1-2-sphere-rendering-notes.md`.
- **Camera / frame data** in a uniform buffer or push constants.
- **Accumulation:** the ray tracer writes an `rgba32f` image of its own (`eStorage` only, kept across frames), and a small pass averages/tonemaps it into the scene target. Reset on camera move or resize.
- **No editor (game build):** a `BlitPass` copies the scene target into the swapchain (`blitImage` converts format and scale; the swapchain needs `eTransferDst` usage; after the blit, transition the swapchain image back to `eColorAttachmentOptimal` if ImGui still draws, or let `End` take it to present).
- **Later, when they hurt:** deferred destruction instead of `waitIdle` on resize; a pool/allocator for descriptors; VMA instead of one `vkAllocateMemory` per resource; passes declaring their reads/writes so barriers are computed (a render graph).

## Open items noticed along the way

- `TrianglePass` holds the decoded pixels for its whole life; free them after the upload (Step 3).
- `.vscode/` changes (macOS-specific `compile_commands.json` path, `c_cpp_properties.json`) are deliberately not committed.
- The swapchain's own image usage is `eColorAttachment` only; add `eTransferDst` with the `BlitPass`.
