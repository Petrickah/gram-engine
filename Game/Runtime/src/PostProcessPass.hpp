#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#ifdef GRAM_VULKAN_RENDERER

// The game's post processing: applies the SSAA Post Processing algorithm from Content/shaders/SSAA.hlsl
template <typename Params>
class PostProcessPass : public Gram::VulkanPass
{
public:
    struct Context
    {
        vk::Extent2D InputExtent;
        vk::Extent2D OutputExtent;
        uint32_t     FrameIndex;
        bool         OutputRecreated;
    };

public:
    using Fill = std::function<Params(const PostProcessPass<Params>::Context&)>;

    std::shared_ptr<Gram::VulkanRenderTarget>& GetOutput() { return m_TargetOutput; }

    PostProcessPass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, std::shared_ptr<Gram::VulkanRenderTarget> input, vk::Format format, Fill fill)
        : PostProcessPass(device, shader, input, std::make_shared<Gram::VulkanRenderTarget>(device, input->GetImage().GetExtent(), format, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled), std::move(fill))
    {
        m_FollowsInput = true;
    }

    PostProcessPass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, const std::shared_ptr<Gram::VulkanRenderTarget>& input, const std::shared_ptr<Gram::VulkanRenderTarget>& output, Fill fill)
        : m_Fill(fill)
        , m_TargetInput(input)
        , m_TargetOutput(output)
        , m_Descriptors(Gram::VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, std::vector<vk::DescriptorSetLayoutBinding>  {
            { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler,  .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
            { .binding = 1, .descriptorType = vk::DescriptorType::eUniformBuffer,         .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment }
        }))
        , m_GraphicsPipeline(Gram::VulkanPipeline::Graphics(device, shader, std::array {m_Descriptors.GetLayout()}, m_TargetOutput->GetFormat(), Gram::VertexBuffer::VertexInputInfo))
        , m_VertexBuffer(Gram::VulkanBuffer::Create<Gram::VertexBuffer>(device, {
            { .Position = { -1.0f, -1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
            { .Position = {  1.0f, -1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 1.0f, 0.0f } },
            { .Position = {  1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 1.0f, 1.0f } },
            { .Position = { -1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 1.0f } }
        }))
        , m_IndexBuffer(Gram::VulkanBuffer::Create<Gram::IndexBuffer>(device, { 0, 1, 2, 2, 3, 0 }))
        , m_Uniforms(Gram::VulkanBuffer::Create<Gram::UniformBuffer<Params>>(device))
    {
        if (m_TargetInput && m_TargetInput->GetImage().IsValid()) m_Descriptors.Write(device, 0, m_TargetInput->GetImage());
        m_Descriptors.Write(device, 1, m_Uniforms, sizeof(Params));
    }

    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override
    {
        GramCheck(m_GraphicsPipeline.IsValid(), "PostProcessPass: the graphics pipeline wasn't created");
        GramCheck(m_TargetOutput && m_TargetInput, "PostProcessPass: at least one of the images were nullptr");
        
        Gram::VulkanImage& sceneOutput = m_TargetOutput->GetImage();
        Gram::VulkanImage& sceneInput  = m_TargetInput->GetImage();

        // The image's own size, not the swapchain's: the post processing pass fills the target, whatever the window.
        const vk::Extent2D outputExtent = sceneOutput.GetExtent();
        const vk::Extent2D inputExtent  = sceneInput.GetExtent();

        const bool outputRecreated = !m_OutputGeneration || *m_OutputGeneration != m_TargetOutput->GetGeneration(); m_OutputGeneration = m_TargetOutput->GetGeneration();
        const bool inputRecreated  = !m_InputGeneration  || *m_InputGeneration  != m_TargetInput->GetGeneration() ; m_InputGeneration  = m_TargetInput->GetGeneration();

        if (m_FollowsInput)
        {
            m_TargetOutput->RequestExtent(m_TargetInput->GetImage().GetExtent());
            m_TargetOutput->ApplyRequestedExtent();
        }

        // The target was recreated (a resize): the sets still point at the old image's view.
        if (inputRecreated)
        {
            // Safe here: ApplyRequestedExtent waited for the device before recreating the image,
            // so no command buffer uses these sets anymore, not even the other frame in flight's.
            m_Descriptors.Write(ctx.Device, 0, sceneInput);
        }
        
        // Transition the image for reading by the fragment shader, ready to apply Post Porcessing effects.
        sceneInput.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);

        // With blending, the old content matters (eLoad), without a new image, which may contain NaN (eClear).
        sceneOutput.BeginRendering(cmd, outputRecreated ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad);

        m_GraphicsPipeline.Bind(cmd);
        m_GraphicsPipeline.BindDescriptorSets(cmd, 0, std::array { m_Descriptors.Get(ctx.ActiveFrame.FrameIndex) });

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

        m_Uniforms[ctx.ActiveFrame.FrameIndex].Send(cmd, [&](vk::CommandBuffer cmd, Gram::VulkanBuffer& buffer)
        {
            const Params bufferData = m_Fill(PostProcessPass<Params>::Context { inputExtent, outputExtent, ctx.ActiveFrame.FrameIndex, outputRecreated });
            buffer.Map<Params, Gram::VulkanBuffer::Access::Write>().Write(&bufferData, 0, 1);
        });

        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(outputExtent.width), static_cast<float>(outputExtent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), outputExtent));
        cmd.drawIndexed(static_cast<uint32_t>(m_IndexBuffer.GetSize() / sizeof(Gram::IndexBuffer::Type)), 1, 0, 0, 0);
        cmd.endRendering();

        // Left ready to sample: ImGui reads it while drawing the viewport panel, inside its own rendering, where no barrier can go.
        sceneOutput.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);

        return true;
    }

private:
    Fill m_Fill;
    
    std::shared_ptr<Gram::VulkanRenderTarget> m_TargetInput;
    std::shared_ptr<Gram::VulkanRenderTarget> m_TargetOutput;
    std::optional<uint64_t>                   m_InputGeneration  = std::nullopt;
    std::optional<uint64_t>                   m_OutputGeneration = std::nullopt;

    Gram::VulkanDescriptors   m_Descriptors;
    Gram::VulkanPipeline      m_GraphicsPipeline;
    Gram::VulkanBuffer        m_VertexBuffer;
    Gram::VulkanBuffer        m_IndexBuffer;
    
    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_Uniforms;
    bool m_FollowsInput = false;
};

#endif // #ifdef GRAM_VULKAN_RENDERER