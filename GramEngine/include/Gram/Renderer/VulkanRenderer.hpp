#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanImGui.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanProfiler.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShader.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

#include <unordered_set>

#ifndef GRAM_EDITOR_ENABLED
#include "Gram/Renderer/VulkanPresentPass.hpp"
#endif // #ifndef GRAM_EDITOR_ENABLED

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class GLFWApplication;

struct ImGuiApplicationLayer;

class VulkanRenderer: public Renderer, public Layer<GLFWApplication>
{
public:
    VulkanRenderer(const VulkanRendererSpecification* rendererSpec);

    template<std::derived_from<VulkanPass> T, typename... Args>
    T& AddPass(Args&&... args)
    {
        return static_cast<T&>(*m_Passes.emplace_back(std::make_unique<T>(std::forward<Args>(args)...)));
    }

public:
    bool OnStart() override;
    bool OnDrawFrame() override;
    bool OnFinish() override;
    bool OnStop() override;

    void OnResize() override { m_VulkanFrame.MarkSwapchainStale(); }

    // What a game needs to build its passes before handing them to AddPass.
    VulkanDevice&          GetDevice()          { return m_VulkanDevice; }
    const VulkanSwapchain& GetSwapchain() const { return m_VulkanSwapchain; }
    // The image a game renders its scene into, shown by the editor's viewport panel.
    std::shared_ptr<VulkanRenderTarget>& GetSceneTarget() { return m_SceneTarget; }
    GramImGuiOnly(LayerOwner<ImGuiApplicationLayer>& GetImGuiApplication() { return m_VulkanImGui; })
    // V-Sync: the swapchain is recreated with the new present mode at the start of the next frame.
    void SetVSync(bool on) { m_VulkanSwapchain.SetVSync(on); m_VulkanFrame.MarkSwapchainStale(); }
    bool GetVSync() const  { return m_VulkanSwapchain.GetVSync(); }
    // Saves the scene target (what the viewport shows, without the editor around it) as a PNG at `path`, at
    // the start of the next frame, before any pass records into it. Logs where it went.
    void RequestScreenshot(std::filesystem::path path) { m_ScreenshotPath = std::move(path); }
    // Each pass's GPU time, shown by the editor's Profiler panel.
    const VulkanProfiler&  GetProfiler() const  { return m_Profiler; }

private:
    bool RecreateSwapchain();
    bool SaveScreenshot(const std::filesystem::path& path);

private:
    VulkanRendererSpecification m_RendererSpec;
    VulkanInstance m_VulkanInstance;
    VulkanSurface m_VulkanSurface;
    VulkanDevice m_VulkanDevice;
    VulkanSwapchain m_VulkanSwapchain;
    VulkanFrame m_VulkanFrame;
    VulkanProfiler m_Profiler;
    std::optional<std::filesystem::path> m_ScreenshotPath;
    std::shared_ptr<VulkanRenderTarget> m_SceneTarget;
    GramImGuiOnly(VulkanImGui m_VulkanImGui);

#ifndef GRAM_EDITOR_ENABLED
    // Without the editor's Viewport (Release, Final), the scene goes straight to the swapchain, under
    // the game's own ImGui panels in Release.
    VulkanPresentPass m_VulkanPresentPass;
#endif // #ifndef GRAM_EDITOR_ENABLED

private:
    std::vector<std::unique_ptr<VulkanPass>> m_Passes;
    // The game's passes whose last Record failed: skipped, not fatal, and logged only when that changes.
    std::unordered_set<const VulkanPass*>    m_FailingPasses;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER