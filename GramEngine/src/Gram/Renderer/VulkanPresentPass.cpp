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
            vk::Offset3D { 0, 0, 0 },
            vk::Offset3D { static_cast<int32_t>(swapchainExtent.width), static_cast<int32_t>(swapchainExtent.height), 1 }
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