#pragma once

#include "Gram/Renderer.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanImGui.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanRenderer: public Renderer, public Layer<Application>
{
public:
    VulkanRenderer(const VulkanRendererSpecification* rendererSpec)
        : m_VulkanInstance(
            rendererSpec
        )
        , m_VulkanSurface(
            rendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance())
        )
        , m_VulkanDevice(
            rendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance()),
            static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface())
        )
        , m_VulkanSwapchain(
            rendererSpec,
            m_VulkanDevice.GetQueueFamilyIndices(),
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()),
            static_cast<vk::PhysicalDevice>(m_VulkanDevice.GetPhysicalDevice()),
            static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface()),
            m_VulkanDevice.AreExtensionsSupported(rendererSpec)
        )
        , m_VulkanPipeline(
            rendererSpec,
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()),
            m_VulkanSwapchain.GetExtent2D(),
            m_VulkanSwapchain.GetSurfaceFormat()
        )
        , m_VulkanFrame(
            rendererSpec,
            m_VulkanDevice.GetQueueFamilyIndices(),
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice())
        )
        , m_VulkanImGui(
            rendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance()),
            static_cast<vk::PhysicalDevice>(m_VulkanDevice.GetPhysicalDevice()),
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()),
            m_VulkanSwapchain.GetSurfaceFormat(),
            m_VulkanDevice.GetQueueFamilyIndices(),
            m_VulkanSwapchain.GetImageCount()
        )
    { }

public:
    bool OnDrawFrame() override;
    bool OnFinish() override;
    void OnResize() override { m_SwapchainStale = true; }

    bool OnStart() override
    {
        GramCheck(CreateRendererObjects(), "couldn't create the renderer objects!\n");
        return true;
    }

    bool OnStop() override
    {
        DestroyRenderFinishedSemaphores();
        DestroyPresentCompleteSemaphores();
        DestroyInFlightFences();
        return true;
    }

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