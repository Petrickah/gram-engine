#pragma once

#include "Renderer.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanInstance.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanSurface.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanRenderer: public Renderer, private UniqueObject
{
public:
    ~VulkanRenderer();
    VulkanRenderer(const RendererSpecification* rendererSpec)
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
    {
        vk::Result result = vk::Result::eSuccess;
        vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
        
        vk::SemaphoreCreateInfo presentCompleteSemaphoreCreateInfo { };
        result = device.createSemaphore(&presentCompleteSemaphoreCreateInfo, nullptr, &m_PresentCompleteSemaphore);
        if (result != vk::Result::eSuccess)
        {
            std::cerr << "[Vulkan] present complete semaphore creation failed: "<< vk::to_string(result) << "!\n";
            return;
        }
        
        for (int index = 0; index < m_VulkanSwapchain.GetImageCount(); index++)
        {
            vk::Semaphore renderFinishedSemaphore;
            vk::SemaphoreCreateInfo renderFinishedSemaphoreCreateInfo { };
            result = device.createSemaphore(&renderFinishedSemaphoreCreateInfo, nullptr, &renderFinishedSemaphore);
            
            if (result != vk::Result::eSuccess)
            {
                break;
            }

            m_RenderFinishedSemaphores.push_back(renderFinishedSemaphore);
        }
        
        if (m_RenderFinishedSemaphores.size() != m_VulkanSwapchain.GetImageCount())
        {
            std::cerr << "[Vulkan] render finish semaphore creation failed: "<< vk::to_string(result) << "!\n";
            return;
        }

        vk::FenceCreateInfo fenceCreateInfo { .flags = vk::FenceCreateFlagBits::eSignaled };
        result = device.createFence(&fenceCreateInfo, nullptr, &m_DrawFence);
        if (result != vk::Result::eSuccess)
        {
            std::cerr << "[Vulkan] draw fence creation failed: "<< vk::to_string(result) << "!\n";
            return;
        }
    }

public:
    bool DrawFrame() override;
    bool Finish() override;

private:
    const VulkanRendererSpecification* m_VulkanRendererSpec;
    VulkanInstance m_VulkanInstance;
    VulkanSurface m_VulkanSurface;
    VulkanDevice m_VulkanDevice;
    VulkanSwapchain m_VulkanSwapchain;
    VulkanPipeline m_VulkanPipeline;
    VulkanFrame m_VulkanFrame;

private:
    vk::Fence     m_DrawFence                = nullptr;
    vk::Semaphore m_PresentCompleteSemaphore = nullptr;
    std::vector<vk::Semaphore> m_RenderFinishedSemaphores;
    bool m_HasFrameStarted = false;
};

inline VulkanRenderer::~VulkanRenderer()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());

    for (size_t index = 0; index < m_RenderFinishedSemaphores.size(); index++)
    {
        device.destroySemaphore(m_RenderFinishedSemaphores[index]);
    }

    if (m_PresentCompleteSemaphore) device.destroySemaphore(m_PresentCompleteSemaphore);
    if (m_DrawFence) device.destroyFence(m_DrawFence);
}

inline bool VulkanRenderer::DrawFrame()
{
    vk::Result             result                   = vk::Result::eSuccess;
    vk::PipelineStageFlags waitDestinationStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::Pipeline           graphicsPipeline         = static_cast<vk::Pipeline>(m_VulkanPipeline.GetPipeline());
    vk::Device             device                   = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    vk::SwapchainKHR       swapChain                = static_cast<vk::SwapchainKHR>(m_VulkanSwapchain.GetSwapchain());
    vk::Extent2D           swapChainExtent          = m_VulkanSwapchain.GetExtent2D();
    vk::CommandBuffer      commandBuffer            = m_VulkanFrame.GetCommandBuffer();

    result = device.waitForFences(1, &m_DrawFence, vk::True, UINT64_MAX);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] wait for fence failed: " << vk::to_string(result) << "\n";
        return false;
    }

    uint32_t imageIndex = 0;
    result = device.acquireNextImageKHR(swapChain, UINT64_MAX, m_PresentCompleteSemaphore, nullptr, &imageIndex);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] acquire image from swapchain failed: " << vk::to_string(result) << "\n";
        return false;
    }

    result = device.resetFences(1, &m_DrawFence);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] reset fences failed: " << vk::to_string(result) << "\n";
        return false;
    }

    const std::vector<vk::Image>&     swapChainImages     = m_VulkanSwapchain.GetSwapchainImages();
    const std::vector<vk::ImageView>& swapChainImageViews = m_VulkanSwapchain.GetSwapchainImageViews();
    if (m_VulkanFrame.StartFrame(imageIndex, swapChainImages, swapChainImageViews, swapChainExtent))
    {
        m_VulkanFrame.RenderFrame(graphicsPipeline, swapChainExtent);

        if (m_VulkanFrame.EndFrame(imageIndex, swapChainImages))
        {
            vk::SubmitInfo submitInfo {
                .waitSemaphoreCount   = 1,
                .pWaitSemaphores      = &m_PresentCompleteSemaphore,
                .pWaitDstStageMask    = &waitDestinationStageMask,
                .commandBufferCount   = 1,
                .pCommandBuffers      = &commandBuffer,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores    = &m_RenderFinishedSemaphores[imageIndex]
            };
    
            vk::PresentInfoKHR presentInfo {
                .waitSemaphoreCount   = 1,
                .pWaitSemaphores      = &m_RenderFinishedSemaphores[imageIndex],
                .swapchainCount       = 1,
                .pSwapchains          = &swapChain,
                .pImageIndices        = &imageIndex,
                .pResults             = nullptr       // Optional
            };

            return m_VulkanDevice.Submit(m_DrawFence, imageIndex, swapChainImages, submitInfo, presentInfo);
        }
    }

    return false;
}

inline bool VulkanRenderer::Finish()
{
    vk::Result       result    = vk::Result::eSuccess;
    vk::Device       device    = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    vk::SwapchainKHR swapChain = static_cast<vk::SwapchainKHR>(m_VulkanSwapchain.GetSwapchain());

    result = device.waitIdle();
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] waiting to finish failed: " << vk::to_string(result) << "\n";
        return false;
    }

    return true;
}

GRAM_NAMESPACE_END