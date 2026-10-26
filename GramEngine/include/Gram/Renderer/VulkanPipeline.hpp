#pragma once

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

struct VulkanRendererSpecification;

class VulkanPipeline : private UniqueObject
{
public:
    VulkanPipeline(const VulkanRendererSpecification *rendererSpec, const vk::Device &device, const vk::Extent2D& extent, const vk::SurfaceFormatKHR& surfaceFormat);
    ~VulkanPipeline();

public:
    VkPipeline GetPipeline() const { return static_cast<VkPipeline>(m_Pipeline); }

private:
    vk::Device m_Device;
    vk::Rect2D m_Scissor;
    vk::Viewport m_Viewport;
    
    std::vector<vk::DynamicState> m_DynamicStates;

    vk::Pipeline m_Pipeline = VK_NULL_HANDLE;
    vk::PipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

    vk::PipelineDynamicStateCreateInfo m_DynamicStateCreateInfo;
    vk::PipelineViewportStateCreateInfo m_ViewportStateCreateInfo;
    vk::PipelineColorBlendAttachmentState m_ColorBlendAttachmentState;
    vk::PipelineColorBlendStateCreateInfo m_ColorBlendStateCreateInfo;
    vk::PipelineMultisampleStateCreateInfo m_MultisampleStateCreateInfo;
    vk::PipelineVertexInputStateCreateInfo m_VertexInputStateCreateInfo;
    vk::PipelineInputAssemblyStateCreateInfo m_InputAssemblyStateCreateInfo;
    vk::PipelineRasterizationStateCreateInfo m_RasterizationStateCreateInfo;
    std::vector<vk::PipelineShaderStageCreateInfo> m_ShaderStageCreateInfos;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER