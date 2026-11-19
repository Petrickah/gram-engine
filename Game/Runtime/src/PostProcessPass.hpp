#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanPass.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShader.hpp"

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

    // An intermediate pass: owns its output, which follows the size of the first input.
    PostProcessPass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, std::vector<std::shared_ptr<Gram::VulkanRenderTarget>> inputs, vk::Format format, Fill fill)
        : PostProcessPass(device, shader, inputs, std::make_shared<Gram::VulkanRenderTarget>(device, inputs.at(0)->GetImage().GetExtent(), format, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled), std::move(fill))
    {
        m_FollowsInput = true;
    }

    // The last pass: writes into someone else's target (the scene), which it doesn't resize.
    // Set 0, binding 0: the uniform. Set 1, binding i: inputs[i], a combined image sampler.
    PostProcessPass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, std::vector<std::shared_ptr<Gram::VulkanRenderTarget>> inputs, const std::shared_ptr<Gram::VulkanRenderTarget>& output, Fill fill)
        : m_Fill(std::move(fill))
        , m_TargetOutput(output)
        , m_TargetInputs(std::move(inputs))
        , m_InputGenerations(m_TargetInputs.size())
        , m_VertexBuffer(Gram::VulkanBuffer::Create<Gram::VertexBuffer>(device, {
            { .Position = { -1.0f, -1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
            { .Position = {  1.0f, -1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 1.0f, 0.0f } },
            { .Position = {  1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 1.0f, 1.0f } },
            { .Position = { -1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 1.0f } }
        }))
        , m_IndexBuffer(Gram::VulkanBuffer::Create<Gram::IndexBuffer>(device, { 0, 1, 2, 2, 3, 0 }))
        , m_Uniforms(Gram::VulkanBuffer::Create<Gram::UniformBuffer<Params>>(device))
        , m_Descriptors(Gram::VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, UniformBindings()))
        , m_ImageDescriptors(Gram::VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, InputBindings(m_TargetInputs.size())))
        , m_GraphicsPipeline(Gram::VulkanPipeline::Graphics(device, shader, std::array { m_Descriptors.GetLayout(), m_ImageDescriptors.GetLayout() }, m_TargetOutput->GetFormat(), Gram::VertexBuffer::VertexInputInfo))
    {
        // The shader's pass file says how many images it samples (Params::InputCount, generated): a different
        // number of inputs here would leave a binding empty or bind one the shader doesn't declare.
        GramCheckVoid(m_TargetInputs.size() == Params::InputCount, "PostProcessPass<{}>: {} inputs given, the shader samples {}", Params::Shader, m_TargetInputs.size(), Params::InputCount);

        m_Descriptors.Write(device, Params::UniformBinding, m_Uniforms, sizeof(Params));

        for (uint32_t inputIndex = 0; inputIndex < m_TargetInputs.size(); inputIndex++)
        {
            if (!m_TargetInputs[inputIndex] || !m_TargetInputs[inputIndex]->GetImage().IsValid()) continue;

            m_ImageDescriptors.Write(device, inputIndex, m_TargetInputs[inputIndex]->GetImage());
        }
    }

    const char* GetName() const override { return Params::Shader; }

    bool Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override
    {
        GramCheck(m_GraphicsPipeline.IsValid(), "PostProcessPass: the graphics pipeline wasn't created");
        GramCheck(m_TargetOutput && m_TargetOutput->GetImage().IsValid(), "PostProcessPass: output image was invalid");

        GramCheck(!m_TargetInputs.empty() && m_TargetInputs[0], "PostProcessPass: no input");

        // Looked up every frame: the inputs are resized with the viewport.
        const vk::Extent2D inputExtent = m_TargetInputs[0]->GetImage().GetExtent();

        if (m_FollowsInput)
        {
            m_TargetOutput->RequestExtent(inputExtent);
            m_TargetOutput->ApplyRequestedExtent();
        }

        Gram::VulkanImage& sceneOutput = m_TargetOutput->GetImage();

        // The image's own size, not the swapchain's: the post processing pass fills the target, whatever the window.
        const vk::Extent2D outputExtent = sceneOutput.GetExtent();

        for (uint32_t inputIndex = 0; inputIndex < m_TargetInputs.size(); inputIndex++)
        {
            GramCheck(m_TargetInputs[inputIndex] && m_TargetInputs[inputIndex]->GetImage().IsValid(), "PostProcessPass: at least one of the images were invalid");

            Gram::VulkanRenderTarget &target     = *m_TargetInputs[inputIndex];
            Gram::VulkanImage        &sceneInput = target.GetImage();

            const bool inputRecreated = !m_InputGenerations[inputIndex] || *m_InputGenerations[inputIndex] != target.GetGeneration() ; m_InputGenerations[inputIndex] = target.GetGeneration();

            // The target was recreated (a resize): the sets still point at the old image's view.
            if (inputRecreated)
            {
                // Safe here: ApplyRequestedExtent waited for the device before recreating the image,
                // so no command buffer uses these sets anymore, not even the other frame in flight's.
                m_ImageDescriptors.Write(ctx.Device, inputIndex, sceneInput);
            }

            // Transition the image for reading by the fragment shader, ready to apply Post Porcessing effects.
            sceneInput.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        }
        
        const bool outputRecreated = !m_OutputGeneration || *m_OutputGeneration != m_TargetOutput->GetGeneration(); m_OutputGeneration = m_TargetOutput->GetGeneration();

        // With blending, the old content matters (eLoad), without a new image, which may contain NaN (eClear).
        sceneOutput.BeginRendering(cmd, outputRecreated ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad);

        m_GraphicsPipeline.Bind(cmd);
        m_GraphicsPipeline.BindDescriptorSets(cmd, 0, std::array { m_Descriptors.Get(ctx.ActiveFrame.FrameIndex), m_ImageDescriptors.Get(ctx.ActiveFrame.FrameIndex) });

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
    // Set 0: the pass's parameters, filled every frame by m_Fill.
    static std::vector<vk::DescriptorSetLayoutBinding> UniformBindings()
    {
        return { { .binding = Params::UniformBinding, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment } };
    }

    // Set 1: one combined image sampler per input, binding i = inputs[i].
    static std::vector<vk::DescriptorSetLayoutBinding> InputBindings(size_t count)
    {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        for (uint32_t inputIndex = 0; inputIndex < count; inputIndex++)
        {
            bindings.push_back({ .binding = inputIndex, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment });
        }
        return bindings;
    }

    Fill m_Fill;
    
    std::shared_ptr<Gram::VulkanRenderTarget>              m_TargetOutput;
    std::vector<std::shared_ptr<Gram::VulkanRenderTarget>> m_TargetInputs;
    std::vector<std::optional<uint64_t>>                   m_InputGenerations;
    std::optional<uint64_t>                                m_OutputGeneration = std::nullopt;
    
    // Declared in the order the constructor builds them: the pipeline needs both layouts.
    Gram::VulkanBuffer        m_VertexBuffer;
    Gram::VulkanBuffer        m_IndexBuffer;
    std::array<Gram::VulkanBuffer, Gram::vkMaxFramesInFlight> m_Uniforms;
    Gram::VulkanDescriptors   m_Descriptors;
    Gram::VulkanDescriptors   m_ImageDescriptors;
    Gram::VulkanPipeline      m_GraphicsPipeline;
    bool m_FollowsInput = false;
};

#endif // #ifdef GRAM_VULKAN_RENDERER