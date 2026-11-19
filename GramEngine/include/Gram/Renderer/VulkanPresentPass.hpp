#pragma once

#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanPresentPass : public Gram::VulkanPass
{
public:
    const char* GetName() const override { return "Present"; }
    VulkanPresentPass(VulkanDevice& device, const std::shared_ptr<VulkanRenderTarget>& target);

    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

private:
    std::shared_ptr<VulkanRenderTarget> m_SceneTarget;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER