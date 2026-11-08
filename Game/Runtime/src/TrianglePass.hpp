#pragma once

#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#ifdef GRAM_VULKAN_RENDERER

namespace Gram {
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanShader;
}

// The game's scene: one triangle from Content/shaders/Default.hlsl, drawn into the renderer's scene
// target (shown by the editor's viewport panel), at whatever size the target currently has.
// Registered by RuntimeApplication through VulkanRenderer::AddPass; the engine only runs it.
class TrianglePass : public Gram::VulkanPass
{
public:
    ~TrianglePass();
    TrianglePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target);

    bool LoadTexture(const std::filesystem::path& workspace, std::string_view imagePath);
    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override;

private:
    void* m_TexturePixels = nullptr;
    vk::DeviceSize m_TextureSize = 0;

    // The renderer's: its image is recreated on resize, so it's looked up every frame, never kept.
    Gram::VulkanRenderTarget& m_Target;
    Gram::VulkanPipeline      m_GraphicsPipeline;
    Gram::VulkanBuffer        m_VertexBuffer;
};

#endif // #ifdef GRAM_VULKAN_RENDERER
