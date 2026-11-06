#pragma once

#include "Gram/Renderer/VulkanRenderer.hpp"

#ifdef GRAM_VULKAN_RENDERER

// The game's scene: one triangle from Content/shaders/Default.hlsl, drawn into the swapchain image.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
class TrianglePass : public Gram::VulkanPass
{
public:
    TrianglePass(Gram::VulkanDevice& device, const Gram::VulkanSwapchain& swapchain, const Gram::VulkanShader& shader);

    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

private:
    Gram::VulkanPipeline m_GraphicsPipeline;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
