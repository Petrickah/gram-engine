#include "TrianglePass.hpp"

#ifdef GRAM_VULKAN_RENDERER

TrianglePass::TrianglePass(Gram::VulkanDevice& device, const Gram::VulkanSwapchain& swapchain, const Gram::VulkanShader& shader)
    : m_GraphicsPipeline(Gram::VulkanPipeline::Graphics(static_cast<vk::Device>(device.GetLogicalDevice()), shader, {}, swapchain.GetSurfaceFormat().format))
{}

bool TrianglePass::Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx)
{
    GramCheck(m_GraphicsPipeline.IsValid(), "TrianglePass: the graphics pipeline wasn't created");

    // Clears or loads depending on whether a pass rendered before this one; see BeginSwapchainRendering.
    BeginSwapchainRendering(ctx);

    const vk::Extent2D& extent = ctx.Swapchain.GetExtent2D();
    m_GraphicsPipeline.Bind(cmd);
    cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
    cmd.draw(3, 1, 0, 0);

    cmd.endRendering();
    return true;
}

#endif // #ifdef GRAM_VULKAN_RENDERER
