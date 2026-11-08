#pragma once

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanDevice;
class VulkanShader;

class VulkanPipeline : private MoveObject
{
public:
    static VulkanPipeline Graphics(
        const VulkanDevice&                      device,
        const VulkanShader&                      shader,
        std::span<const vk::DescriptorSetLayout> setLayouts,
        vk::Format                               colorFormat,
        // How vertex buffers are read (VulkanBuffer::Vertex::GetVertexInputInfo()); the default, empty,
        // is for passes without one (vertices from SV_VertexID, a fullscreen triangle). The pointers in
        // it only have to live until this call returns.
        const vk::PipelineVertexInputStateCreateInfo& vertexInput = {}
    );

    static VulkanPipeline Compute(
        const VulkanDevice&                      device,
        const VulkanShader&                      shader,
        std::span<const vk::DescriptorSetLayout> setLayouts
    );

    VulkanPipeline() = default;
    void Bind(vk::CommandBuffer cmd) const { cmd.bindPipeline(m_BindPoint, *m_Pipeline); }
    void BindDescriptorSets(vk::CommandBuffer cmd, std::span<const vk::DescriptorSet> sets) const { cmd.bindDescriptorSets(m_BindPoint, *m_Layout, 0, sets, {}); }
    bool IsValid() const { return m_Pipeline && m_Layout; }

private:
    VulkanPipeline(vk::UniquePipelineLayout layout, vk::UniquePipeline pipeline, vk::PipelineBindPoint bindPoint);

    vk::PipelineBindPoint    m_BindPoint;
    vk::UniquePipelineLayout m_Layout;
    vk::UniquePipeline       m_Pipeline;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER