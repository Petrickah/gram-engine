#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

bool VulkanRenderer::RecreateSwapchain()
{
    // One per swapchain image, so the count can change; the old ones may also have been left
    // signaled by a present that failed. The device is idle after Recreate(), so they are free.
    GramCheck(m_VulkanSwapchain.Recreate(), "couldn't recreate the swapchain: call to VulkanSwapchain::Recreate failed");
    return m_VulkanFrame.OnRecreateSwapchain(static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()), m_VulkanSwapchain.GetImageCount());
}

VulkanRenderer::VulkanRenderer(const VulkanRendererSpecification *rendererSpec)
    : m_RendererSpec(
        * rendererSpec
    )
    , m_VulkanInstance(
        & m_RendererSpec
    )
    , m_VulkanSurface(
        & m_RendererSpec
        , static_cast<vk::Instance>(m_VulkanInstance.GetInstance())
    )
    , m_VulkanDevice(
        & m_RendererSpec
        , static_cast<vk::Instance>(m_VulkanInstance.GetInstance())
        , static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface())
    )
    , m_VulkanSwapchain(
        & m_RendererSpec
        , m_VulkanDevice.GetQueueFamilyIndices()
        , static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice())
        , static_cast<vk::PhysicalDevice>(m_VulkanDevice.GetPhysicalDevice())
        , static_cast<vk::SurfaceKHR>(m_VulkanSurface.GetSurface())
        , m_VulkanDevice.AreExtensionsSupported(&m_RendererSpec)
    )
    , m_VulkanFrame(
        & m_RendererSpec
        , m_VulkanDevice.GetQueueFamilyIndices()
        , static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice())
    )
    , m_SceneTarget(std::make_shared<Gram::VulkanRenderTarget>(
        m_VulkanDevice
        , vk::Extent2D { 1280, 720 }     // until the viewport panel asks for its own size
        , vk::Format::eR32G32B32A32Sfloat
        , vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc
    ))
    GramImGuiOnly(, m_VulkanImGui(
        & m_RendererSpec
        , static_cast<vk::Instance>(m_VulkanInstance.GetInstance())
        , static_cast<vk::PhysicalDevice>(m_VulkanDevice.GetPhysicalDevice())
        , static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice())
        , m_VulkanSwapchain.GetSurfaceFormat()
        , m_VulkanDevice.GetQueueFamilyIndices()
        , m_VulkanSwapchain.GetImageCount()
    ))
#ifndef GRAM_EDITOR_ENABLED
    , m_VulkanPresentPass(
        m_VulkanDevice
        , m_SceneTarget)
#endif // #ifndef GRAM_EDITOR_ENABLED
{
    // The game adds its own passes (AddPass); the renderer's own come after them, as members: the present
    // pass (no editor: Release, Final), then ImGui (Debug, Release), always drawn last.
}

bool VulkanRenderer::OnDrawFrame()
{
    // Resized, or an earlier acquire/present reported the swapchain out of date. A false here
    // is not fatal: it is a minimized window (or a build error already logged), so skip the
    // frame and retry on the next one.
    if (m_VulkanFrame.IsSwapchainStale() && !RecreateSwapchain()) return true;

    // The size the viewport panel asked for last frame, before any pass renders into the scene.
    if (m_SceneTarget) m_SceneTarget->ApplyRequestedExtent();

    std::optional<VulkanFrame::Active> frame = m_VulkanFrame.Begin(static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()), m_VulkanSwapchain);
    if (!frame) return true;
    
    VulkanPass::Context ctx {
        .Device      = m_VulkanDevice,
        .ActiveFrame = *frame,
        .Swapchain   = m_VulkanSwapchain
    };

    bool allRecorded = true;
    for (auto& pass : m_Passes)
    {
        allRecorded = pass->Record(frame->CommandBuffer, ctx) && allRecorded;
    }
    
#ifndef GRAM_EDITOR_ENABLED
    allRecorded = m_VulkanPresentPass.Record(frame->CommandBuffer, ctx) && allRecorded;
#endif // #ifndef GRAM_EDITOR_ENABLED
    GramImGuiOnly(allRecorded = m_VulkanImGui.Record(frame->CommandBuffer, ctx) && allRecorded);

    // Always End, even after a failed pass: Begin reset the fence and began the command buffer, so
    // leaving now would keep the fence unsignaled and hang the next waitForFences on this frame.
    bool allPresented = m_VulkanFrame.End(*frame, m_VulkanDevice, m_VulkanSwapchain);
    for (auto& pass : m_Passes)
    {
        allPresented = pass->OnPresented() && allPresented;
    }

#ifndef GRAM_EDITOR_ENABLED
    allPresented = m_VulkanPresentPass.OnPresented() && allPresented;
#endif // #ifndef GRAM_EDITOR_ENABLED
    GramImGuiOnly(allPresented = m_VulkanImGui.OnPresented() && allPresented);

    GramCheck(allRecorded, "pass recording failed");
    return allPresented;
}

bool VulkanRenderer::OnFinish()
{
    // waitIdle first: ImGui's Vulkan backend frees buffers the GPU may still be reading.
    // Its Shutdown must run here, before the device is destroyed, or its objects leak.
    if (!m_VulkanFrame.OnFinish(static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()))) return false;

    bool allFinished = true;
    for (auto& pass : m_Passes)
    {
        allFinished = pass->Finish() && allFinished;
    }

#ifndef GRAM_EDITOR_ENABLED
    allFinished = m_VulkanPresentPass.Finish() && allFinished;
#endif // #ifndef GRAM_EDITOR_ENABLED
    GramImGuiOnly(allFinished = m_VulkanImGui.Finish() && allFinished);

    return allFinished;
}

bool VulkanRenderer::OnStart()
{
    return m_VulkanFrame.OnStart(static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()), m_VulkanSwapchain.GetImageCount());
}

bool VulkanRenderer::OnStop()
{
    return m_VulkanFrame.OnStop(static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()));
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER