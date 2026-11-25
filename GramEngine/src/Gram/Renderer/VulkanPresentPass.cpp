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
    m_SceneTarget->GetImage().Blit(cmd, ctx.ActiveFrame.SwapchainImage, vk::Filter::eNearest);

    ctx.ActiveFrame.Frame.MarkSwapchainWritten(ctx.ActiveFrame);

    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER