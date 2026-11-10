#include "RayTracePass.hpp"
#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDescriptors.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"

#ifdef GRAM_VULKAN_RENDERER

RayTracePass::RayTracePass(Gram::VulkanDevice& device, const Gram::VulkanShader& shader, Gram::VulkanRenderTarget& target)
    : m_Target(target)
    , m_TargetGeneration(target.GetGeneration())
    , m_Descriptors(Gram::VulkanDescriptors::Create(device, Gram::vkMaxFramesInFlight, std::vector<vk::DescriptorSetLayoutBinding> {
        { .binding = 0, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute }
    }))
    , m_ComputePipeline(Gram::VulkanPipeline::Compute(device, shader, std::array { m_Descriptors.GetLayout() }))
{
    m_Descriptors.Write(device, 0, m_Target.GetImage(), vk::ImageLayout::eGeneral);
}

bool RayTracePass::Record(vk::CommandBuffer cmd, Gram::VulkanPass::Context ctx)
{
    GramCheck(m_ComputePipeline.IsValid(), "RayTracePass: the compute pipeline wasn't created");
    
    // The image's own size, not the swapchain's: the ray tracer fills the target, whatever the window.
    Gram::VulkanImage& scene = m_Target.GetImage();
    const vk::Extent2D extent = scene.GetExtent();

    scene.Transition(cmd, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);

    // The target was recreated (a resize): the sets still point at the old image's view.
    if (m_TargetGeneration != m_Target.GetGeneration())
    {
        // Safe here: ApplyRequestedExtent waited for the device before recreating the image,
        // so no command buffer uses these sets anymore, not even the other frame in flight's.
        m_Descriptors.Write(ctx.Device, 0, m_Target.GetImage(), vk::ImageLayout::eGeneral);
        m_TargetGeneration = m_Target.GetGeneration();
    }

    m_ComputePipeline.Bind(cmd);
    m_ComputePipeline.BindDescriptorSets(cmd, 0, std::array { m_Descriptors.Get(ctx.ActiveFrame.FrameIndex) });

    cmd.dispatch((extent.width + 7) / 8, (extent.height + 7) / 8, 1);

    // Left ready to sample: ImGui reads it while drawing the viewport panel, inside its own rendering,
    // where no barrier can go.
    scene.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
    return true;
}

#endif // #ifdef GRAM_VULKAN_RENDERER
