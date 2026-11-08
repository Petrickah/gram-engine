#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanFrame::VulkanFrame(const VulkanRendererSpecification *rendererSpec, const QueueFamilyIndices& indices, const vk::Device& device)
{
    if(!rendererSpec || !device || !indices.IsComplete()) return;

    vk::CommandPoolCreateInfo poolInfo {
        .flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = *indices.m_GraphicsComputeFamily,
    };
    
    auto commandPool = device.createCommandPoolUnique(poolInfo);
    GramVulkanCheckVoid(commandPool.result, "command pool creation failed");

    m_CommandPool = std::move(commandPool.value);

    vk::CommandBufferAllocateInfo allocInfo {
        .commandPool        = *m_CommandPool,
        .level              = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = Gram::vkMaxFramesInFlight
    };

    auto commandBuffers = device.allocateCommandBuffersUnique(allocInfo);
    GramVulkanCheckVoid(commandBuffers.result, "command buffer allocation failed");
    m_CommandBuffers = std::move(commandBuffers.value);
}

bool VulkanFrame::CreateRendererObjects(vk::Device device, uint32_t imageCount)
{
    GramCheck(CreateFinishedSemaphores(device, imageCount), "couldn't create the render finished semaphores");
    GramCheck(CreateCompleteSemaphores(device), "couldn't create the present complete semaphores");
    GramCheck(CreateFences(device), "couldn't create the flight fences");
    
    return true;
}

void VulkanFrame::DestroyFinishedSemaphores(vk::Device device)
{
    for (vk::Semaphore semaphore : m_FinishedSemaphores)
    {
        device.destroySemaphore(semaphore);
    }

    m_FinishedSemaphores.clear();
}

void VulkanFrame::DestroyPresentCompleteSemaphores(vk::Device device)
{
    for (vk::Semaphore semaphore : m_CompleteSemaphores)
    {
        device.destroySemaphore(semaphore);
    }

    m_CompleteSemaphores.clear();
}

void VulkanFrame::DestroyFences(vk::Device device)
{
    for (vk::Fence fence : m_InFlightFences)
    {
        device.destroyFence(fence);
    }

    m_InFlightFences.clear();
}

bool VulkanFrame::OnStop(vk::Device device)
{
    DestroyFinishedSemaphores(device);
    DestroyPresentCompleteSemaphores(device);
    DestroyFences(device);
    return true;
}

std::optional<VulkanFrame::Active> VulkanFrame::Begin(vk::Device device, VulkanSwapchain& swapChain)
{
    GramVulkanCheck_(device.waitForFences(1, &m_InFlightFences[m_FrameIndex], vk::True, UINT64_MAX), "VulkanFrame::Begin: wait for fences failed", return std::nullopt);

    vk::Result result = device.acquireNextImageKHR(swapChain.GetSwapchain(), UINT64_MAX, m_CompleteSemaphores[m_FrameIndex], nullptr, &m_ImageIndex);
    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        // Nothing was acquired (the semaphore isn't signaled and the fence is still signaled),
        // so just rebuild and try again next frame.
        m_SwapchainStale = true;
        return std::nullopt;
    }

    // Suboptimal still hands over a usable image; draw this frame, rebuild after presenting.
    if (result == vk::Result::eSuboptimalKHR)
    {
        m_SwapchainStale = true;
    }
    else
    {
        GramVulkanCheck_(result, "acquire image from swapchain failed", return std::nullopt);
    }

    GramVulkanCheck_(device.resetFences(1, &m_InFlightFences[m_FrameIndex]), "reset fences failed", return std::nullopt);

    vk::CommandBufferResetFlags resetFlags = vk::CommandBufferResetFlagBits::eReleaseResources;
    GramVulkanCheck_(m_CommandBuffers[m_FrameIndex]->reset(resetFlags), "resetting the frame failed", return std::nullopt);

    vk::CommandBufferBeginInfo beginInfo {};
    GramVulkanCheck_(m_CommandBuffers[m_FrameIndex]->begin(&beginInfo), "starting the frame failed", return std::nullopt);

    // A new frame for this image: what it held doesn't matter (eUndefined discards it). The stage is
    // the one the acquire semaphore is waited at (pWaitDstStageMask in End), so the first transition
    // of the frame chains to the acquire and can't run before the image is ours.
    VulkanImage::State& swapchainImage = swapChain.GetSwapchainImageState(m_ImageIndex);
    swapchainImage.Layout = vk::ImageLayout::eUndefined;
    swapchainImage.Stage  = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    swapchainImage.Access = vk::AccessFlagBits2::eNone;

    return Active { 
        .CommandBuffer = *m_CommandBuffers[m_FrameIndex],
        .Frame         = *this,
        .FrameIndex    = m_FrameIndex,
        .ImageIndex    = m_ImageIndex,
        .SwapchainImage = swapchainImage
    };
}

void VulkanFrame::BeginSwapchainRendering(VulkanFrame::Active& frame, const VulkanSwapchain& swapChain) const
{
    const vk::AttachmentLoadOp loadOp = frame.SwapchainRendered ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
    frame.SwapchainRendered = true;

    // From whatever the previous pass left (eUndefined at the start of the frame), with a barrier
    // between two passes that both write the attachment.
    frame.SwapchainImage.Transition(frame.CommandBuffer, vk::ImageLayout::eColorAttachmentOptimal,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);

    // Ignored unless loadOp is eClear.
    // The background behind the editor's windows: a gray with a slight blue tint. Linear values:
    // the swapchain is sRGB, so they show brighter, about (90, 93, 100) out of 255.
    vk::ClearColorValue clearColor {};
    clearColor.setFloat32({ 0.10f, 0.11f, 0.13f, 1.0f });

    vk::RenderingAttachmentInfo attachmentInfo {
        .imageView   = swapChain.GetSwapchainImageView(frame.ImageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = loadOp,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = { .color = clearColor },
    };

    vk::RenderingInfo renderingInfo {
        .renderArea           = vk::Rect2D { .offset = {0, 0}, .extent = swapChain.GetExtent2D() },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachmentInfo,
    };

    frame.CommandBuffer.beginRendering(renderingInfo);
}

bool VulkanFrame::End(VulkanFrame::Active& frame, VulkanDevice& device, VulkanSwapchain& swapChain)
{
    // Nothing drew this frame (no passes, no editor): the image is still eUndefined. An empty
    // rendering with eClear gives it defined contents before it's presented.
    if (!frame.SwapchainRendered)
    {
        BeginSwapchainRendering(frame, swapChain);
        frame.CommandBuffer.endRendering();
    }

    // To present, from the layout the last pass left: a color attachment (a pass, ImGui, the clear
    // above) today, a transfer destination once a pass blits into the swapchain.
    frame.SwapchainImage.Transition(frame.CommandBuffer, vk::ImageLayout::ePresentSrcKHR,
        vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlagBits2::eNone);

    GramVulkanCheck(m_CommandBuffers[frame.FrameIndex]->end(), "finishing the frame failed");

    vk::Semaphore completeSemaphore = static_cast<vk::Semaphore>(GetCompleteSemaphore(frame.FrameIndex));
    vk::Semaphore finishedSemaphore = static_cast<vk::Semaphore>(GetFinishedSemaphore(frame.ImageIndex));
    vk::PipelineStageFlags waitDestinationStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submitInfo {
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &completeSemaphore,
        .pWaitDstStageMask    = &waitDestinationStageMask,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &m_CommandBuffers[frame.FrameIndex].get(),
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &finishedSemaphore
    };

    vk::PresentInfoKHR presentInfo {
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &finishedSemaphore,
        .swapchainCount       = 1,
        .pSwapchains          = &swapChain.GetSwapchain(),
        .pImageIndices        = &frame.ImageIndex,
        .pResults             = nullptr       // Optional
    };

    const vk::Result result = device.Submit(static_cast<vk::Fence>(GetInFlightFence(frame.FrameIndex)), submitInfo, presentInfo);

    // The member, not frame.FrameIndex: Active is this frame's copy, the next Begin reads m_FrameIndex.
    // Advanced on the stale path too: the submit happened, so this frame's fence is in use.
    m_FrameIndex = (m_FrameIndex + 1) % Gram::vkMaxFramesInFlight;

    // Suboptimal / out of date mean "recreate the swapchain", not "failed" (see VulkanDevice::Submit).
    if (result == vk::Result::eSuboptimalKHR || result == vk::Result::eErrorOutOfDateKHR)
    {
        m_SwapchainStale = true;
        return true;
    }

    GramVulkanCheck(result, "graphics presentation failed");
    return true;
}

bool VulkanFrame::OnStart(vk::Device device, uint32_t imageCount)
{
    GramCheck(CreateRendererObjects(device, imageCount), "couldn't create the renderer objects!\n");
    return true;
}

bool VulkanFrame::OnRecreateSwapchain(vk::Device device, uint32_t imageCount)
{
    GramCheck(CreateFinishedSemaphores(device, imageCount), "couldn't recreate the swapchain: call to create the render finished semaphores failed");
    m_SwapchainStale = false;
    return true;
}

bool VulkanFrame::OnFinish(vk::Device device)
{
    GramVulkanCheck(device.waitIdle(), "waiting to finish failed");
    return true;
}

bool VulkanFrame::CreateFinishedSemaphores(vk::Device device, uint32_t imageCount)
{
    DestroyFinishedSemaphores(device);
    for (uint32_t index = 0; index < imageCount; index++)
    {
        vk::Semaphore finishedSemaphore;
        vk::SemaphoreCreateInfo finishedSemaphoreCreateInfo { };
        GramVulkanCheck(device.createSemaphore(&finishedSemaphoreCreateInfo, nullptr, &finishedSemaphore), "finish semaphore creation failed");
        
        m_FinishedSemaphores.push_back(finishedSemaphore);
    }

    return true;
}

bool VulkanFrame::CreateCompleteSemaphores(vk::Device device)
{
    auto createCompleteSemaphores = [&device](uint32_t frameIndex, std::vector<vk::Semaphore>& completeSemaphores)
    {
        vk::Semaphore completeSemaphore;
        vk::SemaphoreCreateInfo completeSemaphoreCreateInfo { };
        GramVulkanCheck(device.createSemaphore(&completeSemaphoreCreateInfo, nullptr, &completeSemaphore), "complete semaphore creation failed");

        completeSemaphores[frameIndex] = completeSemaphore;
        return true;
    };

    m_CompleteSemaphores.resize(Gram::vkMaxFramesInFlight);
    for (uint32_t frameIndex = 0; frameIndex < Gram::vkMaxFramesInFlight; frameIndex++)
    {
        GramCheck(createCompleteSemaphores(frameIndex, m_CompleteSemaphores), "");
    }

    return true;
}

bool VulkanFrame::CreateFences(vk::Device device)
{
    auto createFlightFences = [&device](uint32_t frameIndex, std::vector<vk::Fence> &flightFences)
    {
        vk::Fence inFlightFence;
        vk::FenceCreateInfo fenceCreateInfo { .flags = vk::FenceCreateFlagBits::eSignaled };
        GramVulkanCheck(device.createFence(&fenceCreateInfo, nullptr, &inFlightFence), "draw fence creation failed");

        flightFences[frameIndex] = inFlightFence;
        return true;
    };

    m_InFlightFences.resize(Gram::vkMaxFramesInFlight);
    for (uint32_t frameIndex = 0; frameIndex < Gram::vkMaxFramesInFlight; frameIndex++)
    {
        GramCheck(createFlightFences(frameIndex, m_InFlightFences), "couldn't create the flight fence");
    }

    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER