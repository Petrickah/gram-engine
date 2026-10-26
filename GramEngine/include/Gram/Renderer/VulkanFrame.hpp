#pragma once

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

struct QueueFamilyIndices;
struct VulkanRendererSpecification;

class VulkanFrame: private UniqueObject
{
public:
    VulkanFrame(const VulkanRendererSpecification* rendererSpec, const QueueFamilyIndices& indices, const vk::Device& device);
    ~VulkanFrame();

public:
    VkCommandBuffer GetCommandBuffer(uint32_t frameIndex) const { return static_cast<VkCommandBuffer>(m_CommandBuffers[frameIndex]); }

public:
    bool StartFrame(const uint32_t frameIndex, const uint32_t imageIndex, const std::vector<vk::Image> &images, const std::vector<vk::ImageView>& imageViews, const vk::Extent2D& extent);
    bool EndFrame(const uint32_t frameIndex, const uint32_t imageIndex, const std::vector<vk::Image> &images);
    bool RenderFrame(const uint32_t frameIndex, const vk::Pipeline& graphicsPipeline, const vk::Extent2D& extent);

private:
    void TransitionImageLayout(
        const uint32_t                frameIndex,
        const uint32_t                imageIndex,
        const std::vector<vk::Image>& images,
        const vk::ImageLayout         old_layout,
        const vk::ImageLayout         new_layout,
        const vk::AccessFlags2        src_access_mask,
        const vk::AccessFlags2        dst_access_mask,
        const vk::PipelineStageFlags2 src_stage_mask,
        const vk::PipelineStageFlags2 dst_stage_mask);

private:
    vk::Device m_Device;
    vk::CommandPool m_CommandPool;
    std::vector<vk::CommandBuffer> m_CommandBuffers;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER