#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanImGui.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShader.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

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

private:
    bool RecreateSwapchain();

private:
    VulkanRendererSpecification m_RendererSpec;
    VulkanInstance m_VulkanInstance;
    VulkanSurface m_VulkanSurface;
    VulkanDevice m_VulkanDevice;
    VulkanSwapchain m_VulkanSwapchain;
    VulkanFrame m_VulkanFrame;
    std::shared_ptr<VulkanRenderTarget> m_SceneTarget;
    GramImGuiOnly(VulkanImGui m_VulkanImGui);

private:
    std::vector<std::unique_ptr<VulkanPass>> m_Passes;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER