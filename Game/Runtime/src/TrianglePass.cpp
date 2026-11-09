#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanShader.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

#include "TrianglePass.hpp"

#include <glm/gtc/matrix_transform.hpp>

struct UniformBufferObject
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

// The one translation unit that compiles stb_image: its implementation has no include guard,
// so a second STB_IMAGE_IMPLEMENTATION (or this header included again under it) redefines everything.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#ifdef GRAM_VULKAN_RENDERER

TrianglePass::TrianglePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target)
    : m_Uniforms(Gram::VulkanBuffer::Create<UniformBufferObject>(device, *this))
    , m_Target(target)
    // Built for the target's format, not the swapchain's: that's what it renders into.
    // Its vertex input matches m_VertexBuffer's layout (Position, Color).
    , m_GraphicsPipeline(Gram::VulkanPipeline::Graphics(device, shader, GetDescriptorSetLayouts(), target.GetFormat(), Gram::VertexBuffer::VertexInputInfo))
    , m_VertexBuffer(Gram::VulkanBuffer::Create<Gram::VertexBuffer>(device, {
        { .Position = { -1.0f, -1.0f }, .Color = { 1.0f, 1.0f, 1.0f } },
        { .Position = {  1.0f, -1.0f }, .Color = { 1.0f, 0.0f, 0.0f } },
        { .Position = {  1.0f,  1.0f }, .Color = { 0.0f, 1.0f, 0.0f } },
        { .Position = { -1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 1.0f } }
    }))
    , m_IndexBuffer(Gram::VulkanBuffer::Create<Gram::IndexBuffer>(device, { 0, 1, 2, 2, 3, 0 }))
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

    m_VertexBuffer.Send(cmd, [](vk::CommandBuffer cmd, vk::Buffer buffer)
    {
        const vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, 1, &buffer, &offset);
    });

    m_IndexBuffer.Send(cmd, [](vk::CommandBuffer cmd, vk::Buffer buffer)
    {
        const vk::DeviceSize offset = 0;
        cmd.bindIndexBuffer(buffer, offset, Gram::IndexBuffer::IndexType);
    });

    auto& uniformBuffer = m_Uniforms[ctx.ActiveFrame.FrameIndex];
    uniformBuffer.Send(cmd, [&](vk::CommandBuffer cmd, vk::Buffer _)
    {
        static  auto  startTime   = std::chrono::high_resolution_clock::now();
                auto  currentTime = std::chrono::high_resolution_clock::now();
                float time        = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject bufferData
        {
            .model  = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .view   = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            .proj   = glm::perspective(glm::radians(45.0f), static_cast<float>(extent.width) / static_cast<float>(extent.height), 0.1f, 10.0f)
        };

        uniformBuffer.Map<UniformBufferObject, Gram::VulkanBuffer::Access::Write>().Write(&bufferData, 0, 1);
        m_GraphicsPipeline.BindDescriptorSets(cmd, std::array<const vk::DescriptorSet, 1> { GetDescriptorSet(ctx.ActiveFrame.FrameIndex) });
    });

    cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f));
    cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), extent));
    cmd.drawIndexed(static_cast<uint32_t>(m_IndexBuffer.GetSize() / sizeof(Gram::IndexBuffer::Type)), 1, 0, 0, 0);
    cmd.endRendering();

    // Left ready to sample: ImGui reads it while drawing the viewport panel, inside its own rendering,
    // where no barrier can go.
    scene.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    return true;
}

#endif // #ifdef GRAM_VULKAN_RENDERER
