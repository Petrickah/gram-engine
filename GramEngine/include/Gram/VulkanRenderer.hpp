#pragma once

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
        if (!CreateRenderFinishedSemaphores())
        {
            return;
        }

        if (!CreatePresentCompleteSemaphores())
        {
            return;
        }

        if (!CreateInFlightFences())
        { 
            return;
        }
    }

public:
    bool DrawFrame() override;
    bool Finish() override;
    void OnResize() override { m_SwapchainStale = true; }

private:
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

inline VulkanRenderer::~VulkanRenderer()
{
    DestroyRenderFinishedSemaphores();
    DestroyPresentCompleteSemaphores();
    DestroyInFlightFences();
}

inline void VulkanRenderer::DestroyRenderFinishedSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    for (vk::Semaphore semaphore : m_RenderFinishedSemaphores)
    {
        device.destroySemaphore(semaphore);
    }

    m_RenderFinishedSemaphores.clear();
}

inline void VulkanRenderer::DestroyPresentCompleteSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    for (vk::Semaphore semaphore : m_PresentCompleteSemaphores)
    {
        device.destroySemaphore(semaphore);
    }

    m_PresentCompleteSemaphores.clear();
}

inline void VulkanRenderer::DestroyInFlightFences()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    for (vk::Fence fence : m_InFlightDrawFences)
    {
        device.destroyFence(fence);
    }

    m_InFlightDrawFences.clear();
}

inline bool VulkanRenderer::CreateRenderFinishedSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());

    DestroyRenderFinishedSemaphores();
    for (uint32_t index = 0; index < m_VulkanSwapchain.GetImageCount(); index++)
    {
        vk::Semaphore renderFinishedSemaphore;
        vk::SemaphoreCreateInfo renderFinishedSemaphoreCreateInfo { };
        vk::Result result = device.createSemaphore(&renderFinishedSemaphoreCreateInfo, nullptr, &renderFinishedSemaphore);
        if (result != vk::Result::eSuccess)
        {
            std::cerr << "[Vulkan] render finish semaphore creation failed: "<< vk::to_string(result) << "!\n";
            return false;
        }

        m_RenderFinishedSemaphores.push_back(renderFinishedSemaphore);
    }

    return true;
}

inline bool VulkanRenderer::CreatePresentCompleteSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());

    auto createPresentSemaphores = [&device](uint32_t frameIndex, std::vector<vk::Semaphore>& presentSemaphores)
    {
        vk::Semaphore presentCompleteSemaphore;
        vk::SemaphoreCreateInfo presentCompleteSemaphoreCreateInfo { };
        vk::Result result = device.createSemaphore(&presentCompleteSemaphoreCreateInfo, nullptr, &presentCompleteSemaphore);
        if (result != vk::Result::eSuccess)
        {
            std::cerr << "[Vulkan] render finish semaphore creation failed: "<< vk::to_string(result) << "!\n";
            return false;
        }

        presentSemaphores[frameIndex] = presentCompleteSemaphore;
        return true;
    };

    m_PresentCompleteSemaphores.resize(Gram::vkMaxFramesInFlight);
    for (uint32_t frameIndex = 0; frameIndex < Gram::vkMaxFramesInFlight; frameIndex++)
    {
        if (!createPresentSemaphores(frameIndex, m_PresentCompleteSemaphores)) return false;
    }

    return true;
}

inline bool VulkanRenderer::CreateInFlightFences()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    auto createFlightFences = [&device](uint32_t frameIndex, std::vector<vk::Fence> &flightFences)
    {
        vk::Fence inFlightFence;
        vk::FenceCreateInfo fenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled};
        vk::Result result = device.createFence(&fenceCreateInfo, nullptr, &inFlightFence);
        if (result != vk::Result::eSuccess)
        {
            std::cerr << "[Vulkan] draw fence creation failed: " << vk::to_string(result) << "!\n";
            return false;
        }

        flightFences[frameIndex] = inFlightFence;
        return true;
    };

    m_InFlightDrawFences.resize(Gram::vkMaxFramesInFlight);
    for (uint32_t frameIndex = 0; frameIndex < Gram::vkMaxFramesInFlight; frameIndex++)
    {
        if (!createFlightFences(frameIndex, m_InFlightDrawFences))
            return false;
    }
    return true;
}

inline bool VulkanRenderer::RecreateSwapchain()
{
    if (!m_VulkanSwapchain.Recreate()) return false;

    // One per swapchain image, so the count can change; the old ones may also have been left
    // signaled by a present that failed. The device is idle after Recreate(), so they are free.
    if (!CreateRenderFinishedSemaphores()) return false;

    m_SwapchainStale = false;
    return true;
}

inline bool VulkanRenderer::DrawFrame()
{
    // Resized, or an earlier acquire/present reported the swapchain out of date. A false here
    // is not fatal: it is a minimized window (or a build error already logged), so skip the
    // frame and retry on the next one.
    if (m_SwapchainStale && !RecreateSwapchain()) return true;

    vk::Result             result                   = vk::Result::eSuccess;
    vk::PipelineStageFlags waitDestinationStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::Pipeline           graphicsPipeline         = static_cast<vk::Pipeline>(m_VulkanPipeline.GetPipeline());
    vk::Device             device                   = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    vk::SwapchainKHR       swapChain                = static_cast<vk::SwapchainKHR>(m_VulkanSwapchain.GetSwapchain());
    vk::CommandBuffer      commandBuffer            = static_cast<vk::CommandBuffer>(m_VulkanFrame.GetCommandBuffer(m_FrameIndex));
    vk::Extent2D           swapChainExtent          = m_VulkanSwapchain.GetExtent2D();

    result = device.waitForFences(1, &m_InFlightDrawFences[m_FrameIndex], vk::True, UINT64_MAX);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] wait for fence failed: " << vk::to_string(result) << "\n";
        return false;
    }

    uint32_t imageIndex = 0;
    result = device.acquireNextImageKHR(swapChain, UINT64_MAX, m_PresentCompleteSemaphores[m_FrameIndex], nullptr, &imageIndex);
    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        // Nothing was acquired (the semaphore isn't signaled and the fence is still signaled),
        // so just rebuild and try again next frame.
        m_SwapchainStale = true;
        return true;
    }

    // Suboptimal still hands over a usable image; draw this frame, rebuild after presenting.
    if (result == vk::Result::eSuboptimalKHR)
    {
        m_SwapchainStale = true;
    }
    else if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] acquire image from swapchain failed: " << vk::to_string(result) << "\n";
        return false;
    }

    result = device.resetFences(1, &m_InFlightDrawFences[m_FrameIndex]);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] reset fences failed: " << vk::to_string(result) << "\n";
        return false;
    }

    const std::vector<vk::Image>&     swapChainImages     = m_VulkanSwapchain.GetSwapchainImages();
    const std::vector<vk::ImageView>& swapChainImageViews = m_VulkanSwapchain.GetSwapchainImageViews();
    if (m_VulkanFrame.StartFrame(m_FrameIndex, imageIndex, swapChainImages, swapChainImageViews, swapChainExtent))
    {
        m_VulkanFrame.RenderFrame(m_FrameIndex, graphicsPipeline, swapChainExtent);
        m_VulkanImGui.RenderFrame(commandBuffer);

        if (m_VulkanFrame.EndFrame(m_FrameIndex, imageIndex, swapChainImages))
        {
            vk::SubmitInfo submitInfo {
                .waitSemaphoreCount   = 1,
                .pWaitSemaphores      = &m_PresentCompleteSemaphores[m_FrameIndex],
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

            result = m_VulkanDevice.Submit(m_InFlightDrawFences[m_FrameIndex], imageIndex, swapChainImages, submitInfo, presentInfo);
            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
            {
                m_SwapchainStale = true;
                return true;
            }

            if (result != vk::Result::eSuccess)
            {
                std::cerr << "[Vulkan] graphics presentation failed: " << vk::to_string(result) << "!\n";
                return false;
            }

            m_FrameIndex = (m_FrameIndex + 1) % Gram::vkMaxFramesInFlight;
            return true;
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

    m_VulkanImGui.Finish();
    return true;
}

GRAM_NAMESPACE_END