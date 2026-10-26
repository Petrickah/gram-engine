#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanImGui.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class GLFWApplication;
class ImGuiApplicationLayer;

class VulkanRenderer: public Renderer, public Layer<GLFWApplication>
{
public:
    VulkanRenderer(const VulkanRendererSpecification* rendererSpec);

public:
    bool OnStart() override;
    bool OnDrawFrame() override;
    bool OnFinish() override;
    bool OnStop() override;

    void OnResize() override { m_SwapchainStale = true; }
    LayerOwner<ImGuiApplicationLayer>& GetImGuiApplication() { return m_VulkanImGui; }

private:
    bool CreateRendererObjects();
    bool CreateRenderFinishedSemaphores();
    bool CreatePresentCompleteSemaphores();
    bool CreateInFlightFences();
    void DestroyRenderFinishedSemaphores();
    void DestroyPresentCompleteSemaphores();
    void DestroyInFlightFences();
    bool RecreateSwapchain();

private:
    VulkanRendererSpecification m_RendererSpec;
    VulkanInstance m_VulkanInstance;
    VulkanSurface m_VulkanSurface;
    VulkanDevice m_VulkanDevice;
    VulkanSwapchain m_VulkanSwapchain;
    VulkanPipeline m_VulkanPipeline;
    VulkanFrame m_VulkanFrame;
    VulkanImGui m_VulkanImGui;

private:
    std::vector<vk::Fence> m_InFlightDrawFences;
    std::vector<vk::Semaphore> m_PresentCompleteSemaphores;
    std::vector<vk::Semaphore> m_RenderFinishedSemaphores;
    bool m_HasFrameStarted = false;
    bool m_SwapchainStale  = false;
    uint32_t m_FrameIndex  = 0;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER