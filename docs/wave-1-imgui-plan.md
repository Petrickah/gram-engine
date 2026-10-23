# Wave 1 — ImGui debug UI (plan)

Status as of 2026-10-23: **steps 1–5 done** (docking demo window over the triangle). The UI interface from *Architecture* moved to its own task, "Layers" (Task 1.z in `.gram.kanban.md`); step 6 is still open. Originally tracked as the Wave 1 task "ImGui debug UI". Not a new wave: a debug panel that shows ECS entities is useful for Task 1.4 regardless of any editor. A *visual editor* stays a non-goal in `CLAUDE.md` — this plan only lays the foundation (docking, a UI hook, later a scene viewport) without committing to one.

Written against Dear ImGui's Vulkan + GLFW backends. Exact struct fields and function names shift between ImGui releases — check `backends/imgui_impl_vulkan.h` of the version vcpkg installs before writing code.

## Prerequisites

- Finish *frames in flight* (rest of Task 1.2, see `wave-1-hello-triangle.md`). The Vulkan backend keeps per-frame resources sized from the image count it's told, so it should be wired to the final synchronization layout, not rewritten afterwards.
- Logging (Task 1.x) is independent and can come before or after.

## How ImGui fits a frame

ImGui is *immediate mode*: the UI is rebuilt from scratch every frame by plain function calls. A frame has two separate phases, and they live in different places:

1. **Build** (CPU only): `NewFrame()` → any number of `Begin`/`Text`/`Button`/`End` calls → `Render()`. This produces a list of vertices/indices (`ImDrawData`). No Vulkan involved; it can run anywhere in the CPU part of the frame, before the command buffer is recorded.
2. **Record** (GPU commands): `ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer)`, called while the command buffer is inside `beginRendering`, **after** the scene is drawn so the UI lands on top.

That split decides the architecture.

## Architecture

```
Application ──(UI hook, CPU)──► draws widgets
     │
     ▼
Renderer  ── owns ──►  VulkanImGui  (context + GLFW backend + Vulkan backend)
```

- **`VulkanImGui`**: a new renderer component, `UniqueObject`, same conventions as the others (early-return guards, logs on failure, destructor safe when half-built). Constructed after the swapchain and pipeline in `VulkanRenderer`'s member list, so it's destroyed first. It needs the GLFW window, which it takes from `rendererSpec->m_Application` with a `dynamic_cast<GLFWApplication*>` — the same pattern as `VulkanSurface`.
- **Application side talks to an interface, not to Vulkan.** Two reasonable shapes; pick one when implementing:
  - a `std::function<void()>` set with `Renderer::SetUICallback(...)` — smallest, enough for one debug panel;
  - a small `UILayer { virtual void OnUI() = 0; }` registered with `Renderer::AddUILayer(...)` — better once there are several independent panels (entity list, inspector, stats).
- **Hooks in `DrawFrame`:**
  1. *UI hook* (CPU): after the swapchain-stale check and before recording: `NewFrame` ×3 → call the app's UI callback(s) → `ImGui::Render()`.
  2. *Scene recording* stays inside the renderer for now (`VulkanFrame::RenderFrame` draws the triangle). It becomes an app-visible hook only when the scene is driven by entities (Task 1.4) — exposing the raw command buffer to the application earlier would leak Vulkan through the interface.
  3. *UI recording* (GPU): in the same `beginRendering` as the scene, right after the scene's draw call and before `endRendering`. No second rendering pass is needed.
- Skip the whole UI path on frames that are skipped (minimized / swapchain being rebuilt).
- **Input routing:** when `io.WantCaptureMouse` / `io.WantCaptureKeyboard` is true, the game must ignore that input. The renderer or application exposes this so game code can check it.

## Steps

### 1. Dependency
Add `imgui` from vcpkg with the GLFW and Vulkan binding features, plus the docking feature if panels should be dockable (docking lives on ImGui's separate docking branch; vcpkg exposes it as a feature). `gram add imgui --vcpkg` probably won't be enough since `vcpkg.json` needs the object form with `features` — edit `GramEngine/vcpkg.json` by hand and verify the feature names against the port. Link `imgui::imgui` into `GramRenderer`.

### 2. Initialization (order matters)
1. `ImGui::CreateContext()`; set `io.ConfigFlags` (`DockingEnable`, optionally keyboard navigation).
2. `ImGui_ImplGlfw_InitForVulkan(window, install_callbacks)`. With `true`, ImGui installs GLFW callbacks and chains whatever was installed before it — including this project's framebuffer-size callback (set in `GLFWApplication`'s constructor). Verify resize still reaches `Renderer::OnResize()` after wiring this up.
3. `ImGui_ImplVulkan_Init(&initInfo)` with `ImGui_ImplVulkan_InitInfo`: instance, physical device, device, graphics queue family + queue, min image count and image count (from the swapchain), a descriptor pool (or the pool-size field in recent versions), and **`UseDynamicRendering = true`** with a `PipelineRenderingCreateInfo` carrying the swapchain's color format. This project already renders without render passes (dynamic rendering + `synchronization2`), so this is the matching path.
4. Fonts: recent ImGui versions upload the font atlas lazily; older ones need an explicit one-time command buffer.

### 3. Per frame
See *How ImGui fits a frame*. On swapchain recreation, tell the backend the new minimum image count (`ImGui_ImplVulkan_SetMinImageCount`).

### 4. Shutdown
Before the Vulkan device is destroyed, in this order: `waitIdle` → `ImGui_ImplVulkan_Shutdown()` → `ImGui_ImplGlfw_Shutdown()` → `ImGui::DestroyContext()`. `VulkanRenderer::Finish()` already waits for idle; `VulkanImGui`'s destructor does the rest, and being declared after the swapchain/pipeline it runs before them.

### 5. Dockspace
A full-window dockspace (`DockSpaceOverViewport`) gives the editor-style layout where panels attach to the window edges.

### 6. Scene viewport in a panel (needed for an editor, later)
Instead of drawing the scene straight onto the swapchain image, render it into an **offscreen image**, register it with `ImGui_ImplVulkan_AddTexture`, and show it with `ImGui::Image` inside a panel. The image must be recreated when the panel's size changes — the same pattern as swapchain recreation. This needs a real scene (Task 1.4 / Wave 2), so it's the last step and may end up as its own task.

## Interaction with the Rust split (Tasks 1.3 / 1.4)

Keep the ImGui context on the **C++ static side**. `gram-hot` is reloaded at runtime; if panels are written in Rust, they receive what they need (e.g. the context pointer, or an already-built UI description) across the plain `extern "C"` boundary, and call `ImGui::SetCurrentContext` themselves if they use ImGui directly. ImGui's global state must never end up duplicated in a reloaded library.

## Suggested order

Frames in flight → steps 1–4 (a demo window over the triangle, with `ImGui::ShowDemoWindow()`) → the UI interface from *Architecture* → step 5 → step 6 together with the entity-driven scene.

Per the project's methodology (`CLAUDE.md`), steps 2–4 and the per-frame wiring are new Vulkan code, so they're written by hand; the dependency and `vcpkg.json` edit in step 1 are plumbing and can be delegated.
