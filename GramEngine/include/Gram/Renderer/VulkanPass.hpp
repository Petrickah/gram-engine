#pragma once

#include "Gram/Renderer/VulkanFrame.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanDevice;
class VulkanShader;
class VulkanSwapchain;

class VulkanPass
{
public:
    struct Context
    {
              VulkanDevice        &Device;
        // Not const: BeginSwapchainRendering marks the frame as rendered into.
              VulkanFrame::Active &ActiveFrame;
        const VulkanSwapchain     &Swapchain;
    };

public:
    VulkanPass() = default;
    virtual ~VulkanPass() = default;
    virtual bool Record(vk::CommandBuffer cmd, VulkanPass::Context ctx) = 0;
    virtual bool OnPresented() { return true; };
    virtual bool Finish() { return true; };

public:
    inline const vk::DescriptorSet              GetDescriptorSet(uint32_t index) const { return *m_DescriptorSets[index]; }
    inline const std::vector<vk::DescriptorSet> GetDescriptorSets() const
    {
        std::vector<vk::DescriptorSet> sets;
        for (auto& uniqueSet: m_DescriptorSets) sets.push_back(*uniqueSet);
        return sets;
    }
    inline const std::vector<vk::DescriptorSetLayout> GetDescriptorSetLayouts() const
    {
        std::vector<vk::DescriptorSetLayout> setLayouts;
        for (auto& uniqueSetLayout: m_DescriptorSetLayouts) setLayouts.push_back(*uniqueSetLayout);
        return setLayouts;
    }
    inline void SetDescriptorSets(std::vector<vk::UniqueDescriptorSet>&& descriptorSets) 
    {
        m_DescriptorSets = std::move(descriptorSets);
    }
    inline void SetDescriptorSetLayouts(std::vector<vk::UniqueDescriptorSetLayout>&& descriptorSetLayouts) 
    {
        m_DescriptorSetLayouts = std::move(descriptorSetLayouts);
    }
    inline void SetDescriptorPool(vk::UniqueDescriptorPool&& pool)
    {
        m_DescriptorPool = std::move(pool);
    }

protected:
    // See VulkanFrame::BeginSwapchainRendering: the first pass to call it this frame clears, the later
    // ones load. Passes don't pick: their order isn't theirs to know. End it with cmd.endRendering().
    static void BeginSwapchainRendering(const VulkanPass::Context& ctx)
    {
        ctx.ActiveFrame.Frame.BeginSwapchainRendering(ctx.ActiveFrame, ctx.Swapchain);
    }

protected:
    vk::UniqueDescriptorPool m_DescriptorPool;
    std::vector<vk::UniqueDescriptorSetLayout> m_DescriptorSetLayouts;
    std::vector<vk::UniqueDescriptorSet>       m_DescriptorSets;
};

class RayTracePass : public VulkanPass { /* VulkanPipeline::Compute, storage image, descriptors */ };

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER