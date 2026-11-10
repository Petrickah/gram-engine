#pragma once

#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#ifdef GRAM_VULKAN_RENDERER

// The game's scene: one complete scene raytraced from Content/shaders/RayTracer.hlsl, drawn into the
// renderer's scene target (shown by the editor's viewport panel), at whatever size the target currently has.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
class RayTracePass : public Gram::VulkanPass
{
public:
    RayTracePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target);

    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

private:
    // The renderer's: its image is recreated on resize, so it's looked up every frame, never kept.
    Gram::VulkanRenderTarget& m_Target;
    uint64_t                  m_TargetGeneration = 0;
    Gram::VulkanDescriptors   m_Descriptors;
    Gram::VulkanPipeline      m_ComputePipeline;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
