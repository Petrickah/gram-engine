#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanFrame.hpp"

GRAM_NAMESPACE_BEGIN

VulkanFrame::VulkanFrame(const VulkanRendererSpecification *rendererSpec, const QueueFamilyIndices& indices, const vk::Device& device)
    : m_Device(device)
{
    if(!rendererSpec || !device) return;

    vk::CommandPoolCreateInfo poolInfo; {
        poolInfo.flags            = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = indices.m_QueueFamiliesIndices.at("m_GraphicsFamily").value();
    };
    
    vk::Result result = vk::Result::eSuccess;
    result = m_Device.createCommandPool(&poolInfo, nullptr, &m_CommandPool);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] command pool creation failed: " << vk::to_string(result) << "\n";
        return;
    }

    vk::CommandBufferAllocateInfo allocInfo; { 
        allocInfo.commandPool        = m_CommandPool; 
        allocInfo.level              = vk::CommandBufferLevel::ePrimary; 
        allocInfo.commandBufferCount = 1;
    };

    result = m_Device.allocateCommandBuffers(&allocInfo, &m_CommandBuffer);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] command buffer allocation failed: " << vk::to_string(result) << "\n";
        return;
    }
}

VulkanFrame::~VulkanFrame()
{
    if (m_CommandPool) m_Device.destroyCommandPool(m_CommandPool);
}

void VulkanFrame::TransitionImageLayout(
    const uint32_t                imageIndex,
    const std::vector<vk::Image>& images,
    const vk::ImageLayout         old_layout,
    const vk::ImageLayout         new_layout,
    const vk::AccessFlags2        src_access_mask,
    const vk::AccessFlags2        dst_access_mask,
    const vk::PipelineStageFlags2 src_stage_mask,
    const vk::PipelineStageFlags2 dst_stage_mask)
{
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask        = src_stage_mask,
        .srcAccessMask       = src_access_mask,
        .dstStageMask        = dst_stage_mask,
        .dstAccessMask       = dst_access_mask,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = images[imageIndex],
        .subresourceRange    = {
            .aspectMask      = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel    = 0,
            .levelCount      = 1,
            .baseArrayLayer  = 0,
            .layerCount      = 1
        }
    };
    vk::DependencyInfo dependency_info = {
        .dependencyFlags         = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrier
    };

    m_CommandBuffer.pipelineBarrier2(dependency_info);
}

bool VulkanFrame::StartFrame(const uint32_t imageIndex, const std::vector<vk::Image> &images, const std::vector<vk::ImageView> &imageViews, const vk::Extent2D &extent)
{
    vk::CommandBufferBeginInfo beginInfo{};
    vk::Result result = m_CommandBuffer.begin(&beginInfo);
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] starting rendering the frame failed: " << vk::to_string(result) << "\n";
        return false;
    }

    // Before starting rendering, transition the swapchain image to vk::ImageLayout::eColorAttachmentOptimal
    TransitionImageLayout(
        imageIndex,
        images,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::AccessFlags2 {},                                       // srcAccessMask (no need to wait for previous operations)
        vk::AccessFlagBits2::eColorAttachmentWrite,                // dstAccessMask
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,        // srcStage
        vk::PipelineStageFlagBits2::eColorAttachmentOutput         // dstStage
    );

    // Set up the color attachment
    vk::RenderingAttachmentInfo attachmentInfo; {
        attachmentInfo.clearValue.color  = vk::ClearColorValue { .float32 = vk::ArrayWrapper1D<float, 4>({0.0f, 0.0f, 0.0f, 1.0f}) };
        attachmentInfo.imageView         = imageViews[imageIndex];
        attachmentInfo.imageLayout       = vk::ImageLayout::eColorAttachmentOptimal;
        attachmentInfo.loadOp            = vk::AttachmentLoadOp::eClear;
        attachmentInfo.storeOp           = vk::AttachmentStoreOp::eStore;
    };

    // Set up the rendering info
    vk::RenderingInfo renderingInfo; {
        renderingInfo.renderArea           = vk::Rect2D {.offset = {0, 0}, .extent = extent};
		renderingInfo.layerCount           = 1;
		renderingInfo.colorAttachmentCount = 1;
		renderingInfo.pColorAttachments    = &attachmentInfo;
    };

    // Begin rendering
    m_CommandBuffer.beginRendering(renderingInfo);
    return true;
}

bool VulkanFrame::RenderFrame(const vk::Pipeline& graphicsPipeline, const vk::Extent2D& extent)
{
    m_CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
    m_CommandBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
    m_CommandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
    m_CommandBuffer.draw(3, 1, 0, 0);

    return true;
}

bool VulkanFrame::EndFrame(const uint32_t imageIndex, const std::vector<vk::Image> &images)
{
    // End rendering
    m_CommandBuffer.endRendering();

    // After rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR
    TransitionImageLayout(
        imageIndex,
        images,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,             // srcAccessMask
        vk::AccessFlags2 {},                                    // dstAccessMask
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,     // srcStage
        vk::PipelineStageFlagBits2::eBottomOfPipe               // dstStage
    );

    vk::Result result = m_CommandBuffer.end();
    if (result != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] finishing rendering the frame failed: " << vk::to_string(result) << "\n";
        return false;
    }

    return true;
}

GRAM_NAMESPACE_END