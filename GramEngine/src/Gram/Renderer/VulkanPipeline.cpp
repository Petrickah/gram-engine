#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanPipeline.hpp"

#include <utility>

GRAM_NAMESPACE_BEGIN

VulkanPipeline::VulkanPipeline(const VulkanRendererSpecification *rendererSpec, const vk::Device &device, const vk::Extent2D& extent, const vk::SurfaceFormatKHR& surfaceFormat)
    : m_Device                       { device }
    , m_Scissor                      { vk::Offset2D(0, 0), extent }
    , m_DynamicStates                { vk::DynamicState::eViewport, vk::DynamicState::eScissor }
    , m_Viewport                     { 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f }
    , m_InputAssemblyStateCreateInfo { .topology = vk::PrimitiveTopology::eTriangleList }
    , m_VertexInputStateCreateInfo   { }
    , m_ColorBlendAttachmentState    {
        .blendEnable             = vk::True,
        .srcColorBlendFactor     = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor     = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp            = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor     = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor     = vk::BlendFactor::eZero,
        .alphaBlendOp            = vk::BlendOp::eAdd,
        .colorWriteMask          = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    }
    , m_ColorBlendStateCreateInfo    {
        .logicOpEnable           = vk::False,
        .logicOp                 = vk::LogicOp::eCopy,
        .attachmentCount         = 1,
        .pAttachments            = &m_ColorBlendAttachmentState
    }
    , m_DynamicStateCreateInfo       {
        .dynamicStateCount       = static_cast<uint32_t>(m_DynamicStates.size()),
        .pDynamicStates          = m_DynamicStates.data()
    }
    , m_ViewportStateCreateInfo      {
        .viewportCount           = 1,
        .pViewports              = &m_Viewport,
        .scissorCount            = 1,
        .pScissors               = &m_Scissor
    }
    , m_RasterizationStateCreateInfo {
        .depthClampEnable        = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode             = vk::PolygonMode::eFill,
        .cullMode                = vk::CullModeFlagBits::eBack,
        .frontFace               = vk::FrontFace::eClockwise,
        .depthBiasEnable         = vk::False,
        .lineWidth               = 1.0f
    }
    , m_MultisampleStateCreateInfo   {
        .rasterizationSamples    = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable     = vk::False
    }
{
    if (!rendererSpec || !device) return;

    // Only needed until createGraphicsPipelines below; destroyed at the end of the constructor.
    const VulkanShader vertShader(rendererSpec, "shaders/Default.vert.spv", device);
    const VulkanShader fragShader(rendererSpec, "shaders/Default.frag.spv", device);
    if (!vertShader.IsValid() || !fragShader.IsValid()) return;
    
    m_ShaderStageCreateInfos.reserve(2);
    m_ShaderStageCreateInfos.emplace_back(vk::PipelineShaderStageCreateInfo {
        .stage  = vk::ShaderStageFlagBits::eVertex,
        .module = vertShader.GetShaderModule(),
        .pName  = "VSMain"
    });
    m_ShaderStageCreateInfos.emplace_back(vk::PipelineShaderStageCreateInfo {
        .stage  = vk::ShaderStageFlagBits::eFragment,
        .module = fragShader.GetShaderModule(),
        .pName  = "PSMain"
    });

    vk::Result vkResult = vk::Result::eSuccess;
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo
    {
        .setLayoutCount         = 0,
        .pushConstantRangeCount = 0
    };

    vkResult = device.createPipelineLayout(&pipelineLayoutInfo, nullptr, &m_PipelineLayout);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] pipeline layout creation failed: " << vk::to_string(vkResult) << "\n";
        return;
    }

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> pipelineCreateInfoChain =
    {
        {
            .stageCount          = static_cast<uint32_t>(m_ShaderStageCreateInfos.size()),
            .pStages             = m_ShaderStageCreateInfos.data(),
            .pVertexInputState   = &m_VertexInputStateCreateInfo,
            .pInputAssemblyState = &m_InputAssemblyStateCreateInfo,
            .pViewportState      = &m_ViewportStateCreateInfo,
            .pRasterizationState = &m_RasterizationStateCreateInfo,
            .pMultisampleState   = &m_MultisampleStateCreateInfo,
            .pColorBlendState    = &m_ColorBlendStateCreateInfo,
            .pDynamicState       = &m_DynamicStateCreateInfo,
            .layout              = m_PipelineLayout,
            .renderPass          = nullptr
        },
        {
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &surfaceFormat.format
        }
    };

    vkResult = m_Device.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>(), nullptr, &m_Pipeline);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "[Vulkan] pipeline creation failed: " << vk::to_string(vkResult) << "\n";
        return;
    }
}

VulkanPipeline::~VulkanPipeline()
{
    if (m_Pipeline) m_Device.destroyPipeline(m_Pipeline);
    if (m_PipelineLayout) m_Device.destroyPipelineLayout(m_PipelineLayout);
}

GRAM_NAMESPACE_END

