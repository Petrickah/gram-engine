#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"
#include "Gram/Renderer/VulkanShader.hpp"

#include <utility>

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanPipeline VulkanPipeline::Graphics(
    const vk::Device&                        device,
    const VulkanShader&                      shader,
    std::span<const vk::DescriptorSetLayout> setLayouts,
    vk::Format                               colorFormat
)
{
    GramCheck_(shader.IsValid(), return VulkanPipeline({}, {}, vk::PipelineBindPoint::eGraphics), "VulkanPipeline::Graphics: shader program creation failed");

    std::vector<vk::DynamicState> dynamicStates
    {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo
    {
        .setLayoutCount          = static_cast<uint32_t>(setLayouts.size()),
        .pSetLayouts             = setLayouts.data()
    };
    vk::PipelineDynamicStateCreateInfo dynamicStateCreateInfo
    {
        .dynamicStateCount       = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates          = dynamicStates.data()
    };
    vk::PipelineViewportStateCreateInfo viewportStateCreateInfo
    {
        .viewportCount           = 1,
        .scissorCount            = 1,
    };
    vk::PipelineColorBlendAttachmentState colorBlendAttachmentState
    {
        .blendEnable             = vk::True,
        .srcColorBlendFactor     = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor     = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp            = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor     = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor     = vk::BlendFactor::eZero,
        .alphaBlendOp            = vk::BlendOp::eAdd,
        .colorWriteMask          = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };
    vk::PipelineColorBlendStateCreateInfo colorBlendStateCreateInfo
    {
        .logicOpEnable           = vk::False,
        .logicOp                 = vk::LogicOp::eCopy,
        .attachmentCount         = 1,
        .pAttachments            = &colorBlendAttachmentState
    };
    vk::PipelineMultisampleStateCreateInfo multisampleStateCreateInfo
    {
        .rasterizationSamples    = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable     = vk::False
    };
    vk::PipelineVertexInputStateCreateInfo vertexInputStateCreateInfo { };
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo
    {
        .topology                = vk::PrimitiveTopology::eTriangleList
    };
    vk::PipelineRasterizationStateCreateInfo rasterizationStateCreateInfo
    {
        .depthClampEnable        = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eClockwise,
        .depthBiasEnable         = vk::False,
        .lineWidth               = 1.0f
    };

    auto layout = device.createPipelineLayoutUnique(pipelineLayoutInfo);
    GramCheck_(layout.result == vk::Result::eSuccess && layout.has_value(), return VulkanPipeline({}, {}, vk::PipelineBindPoint::eGraphics), "pipeline layout creation failed: {}", vk::to_string(layout.result));

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain 
    {
        vk::GraphicsPipelineCreateInfo {
            .stageCount          = static_cast<uint32_t>(shader.size()),
            .pStages             = shader.data(),
            .pVertexInputState   = &vertexInputStateCreateInfo,
            .pInputAssemblyState = &inputAssemblyStateCreateInfo,
            .pViewportState      = &viewportStateCreateInfo,
            .pRasterizationState = &rasterizationStateCreateInfo,
            .pMultisampleState   = &multisampleStateCreateInfo,
            .pColorBlendState    = &colorBlendStateCreateInfo,
            .pDynamicState       = &dynamicStateCreateInfo,
            .layout              = *layout.value,
            .renderPass          = nullptr
        },
        vk::PipelineRenderingCreateInfo {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat
        }
    };

    auto pipeline = device.createGraphicsPipelineUnique(VK_NULL_HANDLE, pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
    GramCheck_(pipeline.result == vk::Result::eSuccess && pipeline.has_value(), return VulkanPipeline({}, {}, vk::PipelineBindPoint::eGraphics), "pipeline creation failed: {}", vk::to_string(pipeline.result));

    return VulkanPipeline(std::move(layout.value), std::move(pipeline.value), vk::PipelineBindPoint::eGraphics);
}

VulkanPipeline VulkanPipeline::Compute(
    const vk::Device&                        device,
    const VulkanShader&                      shader,
    std::span<const vk::DescriptorSetLayout> setLayouts
)
{
    GramCheck_(shader.IsValid() && shader.size() == 1, return VulkanPipeline({}, {}, vk::PipelineBindPoint::eCompute), "shader loading failed");

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo
    {
        .setLayoutCount          = static_cast<uint32_t>(setLayouts.size()),
        .pSetLayouts             = setLayouts.data()
    };

    auto layout = device.createPipelineLayoutUnique(pipelineLayoutInfo);
    GramCheck_(layout.result == vk::Result::eSuccess && layout.has_value(), return VulkanPipeline({}, {}, vk::PipelineBindPoint::eCompute), "pipeline layout creation failed: {}", vk::to_string(layout.result));

    vk::ComputePipelineCreateInfo pipelineInfo {
        .stage  = shader.data()[0],
        .layout = *layout.value
    };

    auto pipeline = device.createComputePipelineUnique(VK_NULL_HANDLE, pipelineInfo);
    GramCheck_(pipeline.result == vk::Result::eSuccess && pipeline.has_value(), return VulkanPipeline({}, {}, vk::PipelineBindPoint::eCompute), "pipeline creation failed: {}", vk::to_string(pipeline.result));

    return VulkanPipeline(std::move(layout.value), std::move(pipeline.value), vk::PipelineBindPoint::eCompute);
}

VulkanPipeline::VulkanPipeline(vk::UniquePipelineLayout layout, vk::UniquePipeline pipeline, vk::PipelineBindPoint bindPoint)
    : m_BindPoint(bindPoint)
    , m_Layout(std::move(layout))
    , m_Pipeline(std::move(pipeline))
{ }

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
