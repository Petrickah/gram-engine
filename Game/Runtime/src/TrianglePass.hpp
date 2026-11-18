#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanImage.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#include "Passes.generated.hpp"

#ifdef GRAM_VULKAN_RENDERER

// The game's scene: two triangles with a texture from Content/shaders/Default.hlsl, drawn into the
// renderer's scene target (shown by the editor's viewport panel), at whatever size the target currently has.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
class TrianglePass : public Gram::VulkanPass
{
public:
    TrianglePass(const std::filesystem::path& texturePath, Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target);

    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

private:
    // The renderer's: its image is recreated on resize, so it's looked up every frame, never kept.
    Gram::VulkanRenderTarget& m_Target;
    Gram::VulkanImage         m_Texture;
    Gram::VulkanDescriptors   m_Descriptors;        // set 0: the uniform (DefaultParams)
    Gram::VulkanDescriptors   m_ImageDescriptors;   // set 1: the texture
    Gram::VulkanPipeline      m_GraphicsPipeline;
    Gram::VulkanBuffer        m_VertexBuffer;
    Gram::VulkanBuffer        m_IndexBuffer;

    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_Uniforms;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
