#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanImage.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShaderPass.hpp"

#include "Passes.generated.hpp"

#ifdef GRAM_VULKAN_RENDERER

// The game's scene: two triangles with a texture from Content/shaders/Default.hlsl, drawn into the
// renderer's scene target (shown by the editor's viewport panel), at whatever size the target currently has.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
// The graphics-pipeline example: VulkanShaderPass builds its descriptors and pipeline from Default.pass.ron; the
// vertex and index buffers are its own.
class TrianglePass : public Gram::VulkanShaderPass<DefaultParams>
{
public:
    TrianglePass(const std::filesystem::path& texturePath, Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target);

protected:
    void Fill(DefaultParams& params, Gram::VulkanPass::Context ctx) override;
    bool OnRecord(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

private:
    // The renderer's: its image is recreated on resize, so it's looked up every frame, never kept.
    Gram::VulkanRenderTarget& m_Target;
    Gram::VulkanImage         m_Texture;
    Gram::VulkanBuffer        m_VertexBuffer;
    Gram::VulkanBuffer        m_IndexBuffer;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
