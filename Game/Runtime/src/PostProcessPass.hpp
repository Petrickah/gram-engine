#pragma once

#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"
#include "Gram/Renderer/VulkanShader.hpp"
#include "Gram/Renderer/VulkanShaderPass.hpp"

#ifdef GRAM_VULKAN_RENDERER

// One post-process pass: a fullscreen quad drawn with a shader (its .pass.ron says what it samples), its uniform
// filled each frame by `Fill`. Accumulate, DepthOfField, Resample and Sharpen are all one of these.
// The descriptors, the pipeline, the uniform buffers and the inputs are VulkanShaderPass's; what's left here is
// the output it draws into (an attachment, not a descriptor) and the quad.
template <typename Params>
class PostProcessPass : public Gram::VulkanShaderPass<Params>
{
    using Base = Gram::VulkanShaderPass<Params>;

public:
    struct Context
    {
        vk::Extent2D InputExtent;
        vk::Extent2D OutputExtent;
        uint32_t     FrameIndex;
        bool         OutputRecreated;
    };

public:
    using FillFunction = std::function<Params(const PostProcessPass<Params>::Context&)>;

    std::shared_ptr<Gram::VulkanRenderTarget>& GetOutput() { return m_TargetOutput; }

    // An intermediate pass: owns its output, which follows the size of the first input.
    PostProcessPass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, std::vector<std::shared_ptr<Gram::VulkanRenderTarget>> inputs, vk::Format format, FillFunction fill)
        : PostProcessPass(device, shader, inputs, format, inputs.at(0), std::move(fill))
    { }

    // An intermediate pass whose output follows another target's size (the scene's, for an upscaler whose
    // input is smaller than the screen).
    PostProcessPass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, std::vector<std::shared_ptr<Gram::VulkanRenderTarget>> inputs, vk::Format format, std::shared_ptr<Gram::VulkanRenderTarget> sizeOf, FillFunction fill)
        : PostProcessPass(device, shader, inputs, std::make_shared<Gram::VulkanRenderTarget>(device, sizeOf->GetImage().GetExtent(), format, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled), std::move(fill))
    {
        m_SizeOf = std::move(sizeOf);
    }

    // The last pass: writes into someone else's target (the scene), which it doesn't resize.
    // Input i goes to set 1, binding i: the shader's inputs come first there, in the .pass.ron's order.
    PostProcessPass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, std::vector<std::shared_ptr<Gram::VulkanRenderTarget>> inputs, const std::shared_ptr<Gram::VulkanRenderTarget>& output, FillFunction fill)
        : Base(device, shader, output->GetFormat())
        , m_Fill(std::move(fill))
        , m_TargetOutput(output)
        , m_TargetInputs(std::move(inputs))
        , m_VertexBuffer(Gram::VulkanBuffer::Create<Gram::VertexBuffer>(device, {
            { .Position = { -1.0f, -1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 0.0f } },
            { .Position = {  1.0f, -1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 1.0f, 0.0f } },
            { .Position = {  1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 1.0f, 1.0f } },
            { .Position = { -1.0f,  1.0f }, .Color = { 0.0f, 0.0f, 0.0f }, .TexCoord = { 0.0f, 1.0f } }
        }))
        , m_IndexBuffer(Gram::VulkanBuffer::Create<Gram::IndexBuffer>(device, { 0, 1, 2, 2, 3, 0 }))
    {
        // The shader's pass file says how many images it samples (Params::InputCount, generated): a different
        // number of inputs here would leave a binding empty or bind one the shader doesn't declare.
        GramCheckVoid(m_TargetInputs.size() == Params::InputCount, "PostProcessPass<{}>: {} inputs given, the shader samples {}", Params::Shader, m_TargetInputs.size(), Params::InputCount);

        for (uint32_t inputIndex = 0; inputIndex < m_TargetInputs.size(); inputIndex++)
        {
            Base::SetImage(inputIndex, m_TargetInputs[inputIndex]);
        }
    }

protected:
    // Before the base binds the inputs: an output of our own follows the size of the target it was told to.
    bool OnPrepare(Gram::VulkanPass::Context ctx) override
    {
        GramCheck(m_TargetOutput && m_TargetOutput->GetImage().IsValid(), "PostProcessPass<{}>: the output image was invalid", Params::Shader);
        GramCheck(!m_TargetInputs.empty() && m_TargetInputs[0], "PostProcessPass<{}>: no input", Params::Shader);

        if (m_SizeOf)
        {
            m_TargetOutput->RequestExtent(m_SizeOf->GetImage().GetExtent());
            m_TargetOutput->ApplyRequestedExtent();
        }

        m_OutputRecreated = !m_OutputGeneration || *m_OutputGeneration != m_TargetOutput->GetGeneration();
        m_OutputGeneration = m_TargetOutput->GetGeneration();
        return true;
    }

    void Fill(Params& params, Gram::VulkanPass::Context ctx) override
    {
        // Looked up every frame: the inputs are resized with the viewport.
        params = m_Fill(Context {
            .InputExtent     = m_TargetInputs[0]->GetImage().GetExtent(),
            .OutputExtent    = m_TargetOutput->GetImage().GetExtent(),
            .FrameIndex      = ctx.ActiveFrame.FrameIndex,
            .OutputRecreated = m_OutputRecreated,
        });
    }

    // The pipeline and both sets are bound: draw the quad into the output.
    bool OnRecord(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx) override
    {
        Gram::VulkanImage& output = m_TargetOutput->GetImage();

        // The image's own size, not the swapchain's: the post processing pass fills the target, whatever the window.
        const vk::Extent2D extent = output.GetExtent();

        // With blending, the old content matters (eLoad), without a new image, which may contain NaN (eClear).
        output.BeginRendering(cmd, m_OutputRecreated ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad);

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

        // Left ready to sample: ImGui reads it while drawing the viewport panel, inside its own rendering, where no barrier can go.
        output.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        return true;
    }

private:
    FillFunction m_Fill;

    std::shared_ptr<Gram::VulkanRenderTarget>              m_TargetOutput;
    std::vector<std::shared_ptr<Gram::VulkanRenderTarget>> m_TargetInputs;
    std::optional<uint64_t>                                m_OutputGeneration = std::nullopt;
    bool                                                   m_OutputRecreated  = false;

    Gram::VulkanBuffer                        m_VertexBuffer;
    Gram::VulkanBuffer                        m_IndexBuffer;
    std::shared_ptr<Gram::VulkanRenderTarget> m_SizeOf;   // set when the output is our own: whose size it follows
};

#endif // #ifdef GRAM_VULKAN_RENDERER
