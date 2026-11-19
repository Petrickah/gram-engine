#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanImage.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"
#include "Gram/Renderer/VulkanPresentPass.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanPresentPass::VulkanPresentPass(Gram::VulkanDevice& device, const std::shared_ptr<VulkanRenderTarget>& target)
    : m_SceneTarget(target)
{}

bool VulkanPresentPass::Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx)
{
    GramCheck(m_SceneTarget && m_SceneTarget->GetImage().IsValid(), "scene target image is invalid, no image blitting");

    m_SceneTarget->RequestExtent(ctx.Swapchain.GetExtent2D());

    vk::Extent2D sceneExtent     = m_SceneTarget->GetImage().GetExtent();
    vk::Extent2D swapchainExtent = ctx.Swapchain.GetExtent2D();

    m_SceneTarget->GetImage().Transition(cmd, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferRead);
    ctx.ActiveFrame.SwapchainImage.Transition(cmd, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eBlit, vk::AccessFlagBits2::eTransferWrite);

    // The scene as large as fits in the window, centered, its aspect kept: the same size unless a fixed
    // resolution was picked. The bands around it are cleared to black first.
    const float scale = std::min(static_cast<float>(swapchainExtent.width)  / static_cast<float>(sceneExtent.width),
                                 static_cast<float>(swapchainExtent.height) / static_cast<float>(sceneExtent.height));
    const int32_t width  = static_cast<int32_t>(static_cast<float>(sceneExtent.width)  * scale);
    const int32_t height = static_cast<int32_t>(static_cast<float>(sceneExtent.height) * scale);
    const int32_t left   = (static_cast<int32_t>(swapchainExtent.width)  - width)  / 2;
    const int32_t top    = (static_cast<int32_t>(swapchainExtent.height) - height) / 2;
    if (width != static_cast<int32_t>(swapchainExtent.width) || height != static_cast<int32_t>(swapchainExtent.height))
    {
        const vk::ClearColorValue black { std::array { 0.0f, 0.0f, 0.0f, 1.0f } };
        const vk::ImageSubresourceRange all { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1 };
        cmd.clearColorImage(ctx.Swapchain.GetSwapchainImage(ctx.ActiveFrame.ImageIndex), vk::ImageLayout::eTransferDstOptimal, &black, 1, &all);
        const vk::MemoryBarrier2 clearThenBlit {   // the blit writes over part of what the clear wrote
            .srcStageMask = vk::PipelineStageFlagBits2::eClear, .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eBlit,  .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        };
        cmd.pipelineBarrier2(vk::DependencyInfo { .memoryBarrierCount = 1, .pMemoryBarriers = &clearThenBlit });
    }

    vk::ImageBlit2 blitRegion {
        .srcSubresource     = vk::ImageSubresourceLayers {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1 
        },
        .srcOffsets         = std::array {
            vk::Offset3D { 0, 0, 0 },
            vk::Offset3D { static_cast<int32_t>(sceneExtent.width), static_cast<int32_t>(sceneExtent.height), 1 }
        },
        .dstSubresource     = vk::ImageSubresourceLayers {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1 
        },
        .dstOffsets         = std::array {
            vk::Offset3D { left,         top,          0 },
            vk::Offset3D { left + width, top + height, 1 }
        },
    };

    vk::BlitImageInfo2 blitInfo {
        .srcImage       = m_SceneTarget->GetImage().GetHandle(),
        .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
        .dstImage       = ctx.Swapchain.GetSwapchainImage(ctx.ActiveFrame.ImageIndex),
        .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        .regionCount    = 1,
        .pRegions       = &blitRegion,
        .filter         = vk::Filter::eNearest,
    };

    cmd.blitImage2(blitInfo);
    ctx.ActiveFrame.Frame.MarkSwapchainWritten(ctx.ActiveFrame);

    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER