#pragma once
#ifdef GRAM_VULKAN_RENDERER

#include "Renderer.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanImGui.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanRenderer: public Renderer, private UniqueObject
{
public:
    VulkanRenderer(const VulkanRendererSpecification* rendererSpec)
        : m_VulkanRendererSpec(
            dynamic_cast<const VulkanRendererSpecification*>(rendererSpec)
        )
        , m_VulkanInstance(
            m_VulkanRendererSpec
        )
        , m_VulkanSurface(
            m_VulkanRendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance())
        )
        , m_VulkanDevice(
            m_VulkanRendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance()),
            static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface())
        )
        , m_VulkanSwapchain(
            m_VulkanRendererSpec,
            m_VulkanDevice.GetQueueFamilyIndices(),
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()),
            static_cast<vk::PhysicalDevice>(m_VulkanDevice.GetPhysicalDevice()),
            static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface()),
            m_VulkanDevice.AreExtensionsSupported(m_VulkanRendererSpec)
        )
        , m_VulkanPipeline(
            m_VulkanRendererSpec,
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()),
            m_VulkanSwapchain.GetExtent2D(),
            m_VulkanSwapchain.GetSurfaceFormat()
        )
        , m_VulkanFrame(
            m_VulkanRendererSpec,
            m_VulkanDevice.GetQueueFamilyIndices(),
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice())
        )
        , m_VulkanImGui(
            m_VulkanRendererSpec,
            static_cast<vk::Instance>(m_VulkanInstance.GetInstance()),
            static_cast<vk::PhysicalDevice>(m_VulkanDevice.GetPhysicalDevice()),
            static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()),
            m_VulkanSwapchain.GetSurfaceFormat(),
            m_VulkanDevice.GetQueueFamilyIndices(),
            m_VulkanSwapchain.GetImageCount()
        )
    {
        if (!CreateRendererObjects()) return;
    }
    
    ~VulkanRenderer()
    {
        DestroyRenderFinishedSemaphores();
        DestroyPresentCompleteSemaphores();
        DestroyInFlightFences();
    }

public:
    bool DrawFrame() override;
    bool Finish() override;
    void OnResize() override { m_SwapchainStale = true; }

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
    const VulkanRendererSpecification* m_VulkanRendererSpec;
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