#pragma once

#include "Gram/Renderer/VulkanShader.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanPipeline : private MoveObject
{
public:
    static VulkanPipeline Graphics(
        const vk::Device&                        device,
        const VulkanShader&                      shader,
        std::span<const vk::DescriptorSetLayout> setLayouts,
        vk::Format                               colorFormat
    );

    static VulkanPipeline Compute(
        const vk::Device&                        device,
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