#include "TrianglePass.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanImage.hpp"
#include "Gram/Renderer/VulkanShader.hpp"

#include <glm/gtc/matrix_transform.hpp>

#ifdef GRAM_VULKAN_RENDERER

// Built for the target's format, not the swapchain's: that's what it renders into.
TrianglePass::TrianglePass(const std::filesystem::path& texturePath, Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target)
    : VulkanShaderPass(device, shader, target.GetFormat())
    , m_Target(target)
    , m_Texture(Gram::VulkanImage::Load(device, texturePath))
    , m_VertexBuffer(Gram::VulkanBuffer::Create<Gram::VertexBuffer>(device, {
        { .Position = { -1.0f, -1.0f }, .Color = { 1.0f, 1.0f, 1.0f }, .TexCoord = { 0.0f, 0.0f } },
        { .Position = {  1.0f, -1.0f }, .Color = { 1.0f, 0.0f, 0.0f }, .TexCoord = { 1.0f, 0.0f } },
        { .Position = {  1.0f,  1.0f }, .Color = { 0.0f, 1.0f, 0.0f }, .TexCoord = { 1.0f, 1.0f } },
        { .Position = { -1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 1.0f }, .TexCoord = { 0.0f, 1.0f } }
    }))
    , m_IndexBuffer(Gram::VulkanBuffer::Create<Gram::IndexBuffer>(device, { 0, 1, 2, 2, 3, 0 }))
{
    if (m_Texture.IsValid()) SetImage(DefaultParams::AlbedoBinding, m_Texture);
}

void TrianglePass::Fill(DefaultParams& params, Gram::VulkanPass::Context ctx)
{
    static  auto startTime   = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            auto time        = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    // The image's own size, not the swapchain's: the triangle fills the target, whatever the window.
    const vk::Extent2D extent = m_Target.GetImage().GetExtent();
    params = DefaultParams
    {
        .Model  = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .View   = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
        .Proj   = glm::perspective(glm::radians(45.0f), static_cast<float>(extent.width) / static_cast<float>(extent.height), 0.1f, 10.0f)
    };
}

bool TrianglePass::OnRecord(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx)
{
    Gram::VulkanImage& scene = m_Target.GetImage();
    const vk::Extent2D extent = scene.GetExtent();
    scene.BeginRendering(cmd, vk::AttachmentLoadOp::eClear);

    m_VertexBuffer.Send(cmd, [](vk::CommandBuffer cmd, Gram::VulkanBuffer& buffer)
    {
        const vk::DeviceSize offset = 0;
        cmd.bindVertexBuffers(0, 1, &buffer.Get(), &offset);
    });

    m_IndexBuffer.Send(cmd, [](vk::CommandBuffer cmd, Gram::VulkanBuffer& buffer)
    {
        const vk::DeviceSize offset = 0;
        cmd.bindIndexBuffer(buffer.Get(), offset, Gram::IndexBuffer::IndexType);
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
