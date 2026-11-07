#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanShader.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

#include "TrianglePass.hpp"

// The one translation unit that compiles stb_image: its implementation has no include guard,
// so a second STB_IMAGE_IMPLEMENTATION (or this header included again under it) redefines everything.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef GRAM_VULKAN_RENDERER

TrianglePass::TrianglePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target)
    : m_Target(target)
    , m_VertexBuffer(Gram::VulkanBuffer::Vertex::Create(device, {
        { .Position = {  0.0f, -0.5f }, .Color = { 1.0f, 0.0f, 0.0f } },
        { .Position = {  0.5f,  0.5f }, .Color = { 0.0f, 1.0f, 0.0f } },
        { .Position = { -0.5f,  0.5f }, .Color = { 0.0f, 0.0f, 1.0f } }
    }))
    // Built for the target's format, not the swapchain's: that's what it renders into.
    // Its vertex input matches m_VertexBuffer's layout (Position, Color).
    , m_GraphicsPipeline(Gram::VulkanPipeline::Graphics(static_cast<vk::Device>(device.GetLogicalDevice()), shader, {}, target.GetFormat(),
                                                        Gram::VulkanBuffer::Vertex::GetVertexInputInfo()))
{}

TrianglePass::~TrianglePass()
{
    stbi_image_free(m_TexturePixels);
}

bool TrianglePass::LoadTexture(const std::filesystem::path &workspace, std::string_view imagePath)
{
    int texWidth, texHeight, texChannels;
    std::filesystem::path imageLocation = workspace / imagePath;

    GramCheck(std::filesystem::exists(imageLocation), "TrianglePass::LoadTexture: image not found {}", imageLocation.string());

    m_TexturePixels = (void*)stbi_load(imageLocation.string().c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    m_TextureSize   = texWidth * texHeight * 4;

    return m_TexturePixels;
}

bool TrianglePass::Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx)
{
    GramCheck(m_GraphicsPipeline.IsValid(), "TrianglePass: the graphics pipeline wasn't created");

    Gram::VulkanImage& scene = m_Target.GetImage();
    scene.BeginRendering(cmd, vk::AttachmentLoadOp::eClear);

    // The image's own size, not the swapchain's: the triangle fills the target, whatever the window.
    const vk::Extent2D extent = scene.GetExtent();
    m_GraphicsPipeline.Bind(cmd);
    m_VertexBuffer.BindVertex(cmd);
    cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
    cmd.draw(3, 1, 0, 0);
    cmd.endRendering();

    // Left ready to sample: ImGui reads it while drawing the viewport panel, inside its own rendering,
    // where no barrier can go.
    scene.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    return true;
}

#endif // #ifdef GRAM_VULKAN_RENDERER
