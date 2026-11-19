#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanRenderer.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"

// stb_image_write's code, compiled once, here (its reader, stb_image, is compiled in VulkanImage.cpp).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

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
    m_Profiler.Create(m_VulkanDevice);
}

bool VulkanRenderer::OnDrawFrame()
{
    // Resized, or an earlier acquire/present reported the swapchain out of date. A false here
    // is not fatal: it is a minimized window (or a build error already logged), so skip the
    // frame and retry on the next one.
    if (m_VulkanFrame.IsSwapchainStale() && !RecreateSwapchain()) return true;

    // A screenshot asked for while drawing the UI last frame: taken now, from the last frame's finished image,
    // before this frame's passes start recording into it.
    if (m_ScreenshotPath)
    {
        SaveScreenshot(*m_ScreenshotPath);
        m_ScreenshotPath.reset();
    }

    // The size the viewport panel asked for last frame, before any pass renders into the scene.
    if (m_SceneTarget) m_SceneTarget->ApplyRequestedExtent();

    std::optional<VulkanFrame::Active> frame = m_VulkanFrame.Begin(static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice()), m_VulkanSwapchain);
    if (!frame) return true;

    m_Profiler.BeginFrame(frame->CommandBuffer, frame->FrameIndex);
    
    VulkanPass::Context ctx {
        .Device      = m_VulkanDevice,
        .ActiveFrame = *frame,
        .Swapchain   = m_VulkanSwapchain
    };

    bool allRecorded = true;
    for (auto& pass : m_Passes)
    {
        m_Profiler.Begin(frame->CommandBuffer, pass->GetName());
        allRecorded = pass->Record(frame->CommandBuffer, ctx) && allRecorded;
        m_Profiler.End(frame->CommandBuffer);
    }

    
#ifndef GRAM_EDITOR_ENABLED
    m_Profiler.Begin(frame->CommandBuffer, m_VulkanPresentPass.GetName());
    allRecorded = m_VulkanPresentPass.Record(frame->CommandBuffer, ctx) && allRecorded;
    m_Profiler.End(frame->CommandBuffer);
#endif // #ifndef GRAM_EDITOR_ENABLED

#ifdef GRAM_IMGUI_ENABLED
    m_Profiler.Begin(frame->CommandBuffer, m_VulkanImGui.GetName());
    allRecorded = m_VulkanImGui.Record(frame->CommandBuffer, ctx) && allRecorded;
    m_Profiler.End(frame->CommandBuffer);
#endif // #ifdef GRAM_IMGUI_ENABLED

    // Always End, even after a failed pass: Begin reset the fence and began the command buffer, so
    // leaving now would keep the fence unsignaled and hang the next waitForFences on this frame.
    bool allPresented = m_VulkanFrame.End(*frame, m_VulkanDevice, m_VulkanSwapchain);
    // After End the command buffer is closed and submitted: no timestamps from here (OnPresented is CPU work).
    for (auto& pass : m_Passes) allPresented = pass->OnPresented() && allPresented;

#ifndef GRAM_EDITOR_ENABLED
    allPresented = m_VulkanPresentPass.OnPresented() && allPresented;
#endif // #ifndef GRAM_EDITOR_ENABLED

#ifdef GRAM_IMGUI_ENABLED
    allPresented = m_VulkanImGui.OnPresented() && allPresented;
#endif // #ifdef GRAM_IMGUI_ENABLED

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

// Copies the scene target into a host-visible buffer, converts its linear float color to 8-bit sRGB (what
// the swapchain shows: it encodes sRGB itself) and writes it as a PNG.
bool VulkanRenderer::SaveScreenshot(const std::filesystem::path& path)
{
    GramCheck(m_SceneTarget && m_SceneTarget->GetImage().IsValid(), "screenshot: no scene image");
    GramCheck(m_SceneTarget->GetFormat() == vk::Format::eR32G32B32A32Sfloat, "screenshot: the scene isn't RGBA 32-bit float");

    // The frames in flight may still read or write the image: let them finish first.
    const vk::Device device = static_cast<vk::Device>(m_VulkanDevice.GetLogicalDevice());
    GramVulkanCheck(device.waitIdle(), "screenshot: waiting for the device failed");

    VulkanImage&       image  = m_SceneTarget->GetImage();
    const vk::Extent2D extent = image.GetExtent();
    const size_t       count  = static_cast<size_t>(extent.width) * extent.height * 4;   // floats: RGBA per pixel

    VulkanBuffer staging = VulkanBuffer::Create(m_VulkanDevice, count * sizeof(float),
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, vk::BufferUsageFlagBits::eTransferDst);

    GramVulkanCheck(m_VulkanDevice.Submit([&](vk::CommandBuffer cmd)
    {
        image.Transition(cmd, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
        const vk::BufferImageCopy region {
            .imageSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1 },
            .imageExtent      = { extent.width, extent.height, 1 },
        };
        cmd.copyImageToBuffer(image.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, staging.Get(), 1, &region);
        // Back to how the frame left it: sampled by the viewport (or blitted by the present pass).
        image.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        return vk::Result::eSuccess;
    }), "screenshot: copying the scene failed");

    std::vector<float> pixels(count);
    staging.Map<float, VulkanBuffer::Access::Read>().Read(pixels.data(), 0, count);

    // Linear → sRGB, as the swapchain does when it shows the image, so the file looks like the viewport.
    const auto encode = [](float linear)
    {
        const float c = std::clamp(linear, 0.0f, 1.0f);
        const float srgb = c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        return static_cast<uint8_t>(std::lround(srgb * 255.0f));
    };
    std::vector<uint8_t> bytes(count);
    for (size_t i = 0; i < count; i += 4)
    {
        bytes[i + 0] = encode(pixels[i + 0]);
        bytes[i + 1] = encode(pixels[i + 1]);
        bytes[i + 2] = encode(pixels[i + 2]);
        bytes[i + 3] = 255;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const bool written = stbi_write_png(path.string().c_str(), static_cast<int>(extent.width), static_cast<int>(extent.height), 4, bytes.data(), static_cast<int>(extent.width) * 4) != 0;
    GramCheck(written, "screenshot: couldn't write {}", path.string());

    GramInfo("screenshot saved: {} ({}x{})", path.string(), extent.width, extent.height);
    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER