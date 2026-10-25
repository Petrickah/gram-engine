#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

bool VulkanRenderer::CreateRendererObjects()
{
    GramCheck(CreateRenderFinishedSemaphores(), "couldn't create the render finished semaphores");
    GramCheck(CreatePresentCompleteSemaphores(), "couldn't create the present complete semaphores");
    GramCheck(CreateInFlightFences(), "couldn't create the flight fences");
    
    return true;
}

void VulkanRenderer::DestroyRenderFinishedSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    for (vk::Semaphore semaphore : m_RenderFinishedSemaphores)
    {
        device.destroySemaphore(semaphore);
    }

    m_RenderFinishedSemaphores.clear();
}

void VulkanRenderer::DestroyPresentCompleteSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    for (vk::Semaphore semaphore : m_PresentCompleteSemaphores)
    {
        device.destroySemaphore(semaphore);
    }

    m_PresentCompleteSemaphores.clear();
}

void VulkanRenderer::DestroyInFlightFences()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    for (vk::Fence fence : m_InFlightDrawFences)
    {
        device.destroyFence(fence);
    }

    m_InFlightDrawFences.clear();
}

bool VulkanRenderer::CreateRenderFinishedSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());

    DestroyRenderFinishedSemaphores();
    for (uint32_t index = 0; index < m_VulkanSwapchain.GetImageCount(); index++)
    {
        vk::Semaphore renderFinishedSemaphore;
        vk::SemaphoreCreateInfo renderFinishedSemaphoreCreateInfo { };
        GramVulkanCheck(device.createSemaphore(&renderFinishedSemaphoreCreateInfo, nullptr, &renderFinishedSemaphore), "render finish semaphore creation failed");
        
        m_RenderFinishedSemaphores.push_back(renderFinishedSemaphore);
    }

    return true;
}

bool VulkanRenderer::CreatePresentCompleteSemaphores()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());

    auto createPresentSemaphores = [&device](uint32_t frameIndex, std::vector<vk::Semaphore>& presentSemaphores)
    {
        vk::Semaphore presentCompleteSemaphore;
        vk::SemaphoreCreateInfo presentCompleteSemaphoreCreateInfo { };
        GramVulkanCheck(device.createSemaphore(&presentCompleteSemaphoreCreateInfo, nullptr, &presentCompleteSemaphore), "present complete semaphore creation failed");

        presentSemaphores[frameIndex] = presentCompleteSemaphore;
        return true;
    };

    m_PresentCompleteSemaphores.resize(Gram::vkMaxFramesInFlight);
    for (uint32_t frameIndex = 0; frameIndex < Gram::vkMaxFramesInFlight; frameIndex++)
    {
        GramCheck(createPresentSemaphores(frameIndex, m_PresentCompleteSemaphores), "");
    }

    return true;
}

bool VulkanRenderer::CreateInFlightFences()
{
    vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    auto createFlightFences = [&device](uint32_t frameIndex, std::vector<vk::Fence> &flightFences)
    {
        vk::Fence inFlightFence;
        vk::FenceCreateInfo fenceCreateInfo { .flags = vk::FenceCreateFlagBits::eSignaled };
        GramVulkanCheck(device.createFence(&fenceCreateInfo, nullptr, &inFlightFence), "draw fence creation failed");

        flightFences[frameIndex] = inFlightFence;
        return true;
    };

    m_InFlightDrawFences.resize(Gram::vkMaxFramesInFlight);
    for (uint32_t frameIndex = 0; frameIndex < Gram::vkMaxFramesInFlight; frameIndex++)
    {
        GramCheck(createFlightFences(frameIndex, m_InFlightDrawFences), "couldn't create the flight fence");
    }

    return true;
}

bool VulkanRenderer::RecreateSwapchain()
{
    // One per swapchain image, so the count can change; the old ones may also have been left
    // signaled by a present that failed. The device is idle after Recreate(), so they are free.
    GramCheck(m_VulkanSwapchain.Recreate(), "couldn't recreate the swapchain: call to VulkanSwapchain::Recreate failed");
    GramCheck(CreateRenderFinishedSemaphores(), "couldn't recreate the swapchain: call to create the render finished semaphores failed");

    m_SwapchainStale = false;
    return true;
}

bool VulkanRenderer::OnDrawFrame()
{
    // Resized, or an earlier acquire/present reported the swapchain out of date. A false here
    // is not fatal: it is a minimized window (or a build error already logged), so skip the
    // frame and retry on the next one.
    if (m_SwapchainStale && !RecreateSwapchain()) return true;

    vk::PipelineStageFlags waitDestinationStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::Pipeline           graphicsPipeline         = static_cast<vk::Pipeline>(m_VulkanPipeline.GetPipeline());
    vk::Device             device                   = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    vk::SwapchainKHR       swapChain                = static_cast<vk::SwapchainKHR>(m_VulkanSwapchain.GetSwapchain());
    vk::CommandBuffer      commandBuffer            = static_cast<vk::CommandBuffer>(m_VulkanFrame.GetCommandBuffer(m_FrameIndex));
    vk::Extent2D           swapChainExtent          = m_VulkanSwapchain.GetExtent2D();

    GramVulkanCheck(device.waitForFences(1, &m_InFlightDrawFences[m_FrameIndex], vk::True, UINT64_MAX), "wait for fence failed");

    uint32_t imageIndex = 0;
    vk::Result result = device.acquireNextImageKHR(swapChain, UINT64_MAX, m_PresentCompleteSemaphores[m_FrameIndex], nullptr, &imageIndex);

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
    else
    {
        GramVulkanCheck(result, "acquire image from swapchain failed");
    }

    GramVulkanCheck(device.resetFences(1, &m_InFlightDrawFences[m_FrameIndex]), "reset fences failed");

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

            vk::Result result = m_VulkanDevice.Submit(m_InFlightDrawFences[m_FrameIndex], imageIndex, swapChainImages, submitInfo, presentInfo);

            // Nothing was acquired (the semaphore isn't signaled and the fence is still signaled),
            // so just rebuild and try again next frame. Suboptimal still hands over a usable image;
            // draw this frame, rebuild after presenting.
            if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
            {
                m_SwapchainStale = true;
                return true;
            }
            
            GramVulkanCheck(result, "graphics presentation failed");

            m_FrameIndex = (m_FrameIndex + 1) % Gram::vkMaxFramesInFlight;
            return true;
        }
    }

    return false;
}

bool VulkanRenderer::OnFinish()
{
    vk::Device       device    = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    vk::SwapchainKHR swapChain = static_cast<vk::SwapchainKHR>(m_VulkanSwapchain.GetSwapchain());

    GramVulkanCheck(device.waitIdle(), "waiting to finish failed");

    m_VulkanImGui.Finish();
    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER