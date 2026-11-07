#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanImage.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanImage VulkanImage::Create(VulkanDevice &device, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage)
{
    vk::ImageCreateInfo imageInfo {
        .imageType   = vk::ImageType::e2D,
        .format      = format,
        .extent      = { extent.width, extent.height, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = vk::SampleCountFlagBits::e1,
        .tiling      = vk::ImageTiling::eOptimal,
        .usage       = usage,
        .sharingMode = vk::SharingMode::eExclusive
    };

    vk::Device logicalDevice = static_cast<vk::Device>(device.GetLogicalDevice());

    auto image = logicalDevice.createImageUnique(imageInfo);
    GramVulkanCheck_(image.result, "error creating the image instance", return VulkanImage {});

    vk::MemoryRequirements memRequirements = logicalDevice.getImageMemoryRequirements(*image.value);
    const std::optional<uint32_t> memoryType = device.FindMemoryType(memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
    GramCheck_(memoryType.has_value(), return VulkanImage {}, "no device-local memory type fits the image");

    vk::MemoryAllocateInfo allocInfo {
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = *memoryType
    };

    auto memory = logicalDevice.allocateMemoryUnique(allocInfo);
    GramVulkanCheck_(memory.result, "error allocating memory", return VulkanImage {});

    GramVulkanCheck_(logicalDevice.bindImageMemory(*image.value, *memory.value, 0), "error binding the image memory", return VulkanImage {});

    vk::ImageViewCreateInfo imageViewInfo {
        .image    = *image.value,
        .viewType = vk::ImageViewType::e2D,
        .format   = format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    auto imageView = logicalDevice.createImageViewUnique(imageViewInfo);
    GramVulkanCheck_(imageView.result, "error creating the image view", return VulkanImage {});

    vk::SamplerCreateInfo samplerInfo {
        .magFilter     = vk::Filter::eLinear,                  // la mărire (panoul mai mare decât imaginea)
        .minFilter     = vk::Filter::eLinear,                  // la micșorare
        .mipmapMode    = vk::SamplerMipmapMode::eLinear,
        .addressModeU  = vk::SamplerAddressMode::eClampToEdge, // fără repetare la margini
        .addressModeV  = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW  = vk::SamplerAddressMode::eClampToEdge,
        .maxLod        = 0.0f,                                 // un singur nivel de mip
    };
    auto sampler = logicalDevice.createSamplerUnique(samplerInfo);
    GramVulkanCheck_(sampler.result, "error creating the sampler", return VulkanImage {});

    return VulkanImage { std::move(image.value), std::move(memory.value), std::move(imageView.value), std::move(sampler.value), extent, format };
}

void VulkanImage::BeginRendering(vk::CommandBuffer cmd, vk::AttachmentLoadOp loadOp)
{
    m_State.Transition(cmd, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);

    // Ignored unless loadOp is eClear.
    vk::ClearColorValue clearColor {};
    clearColor.setFloat32({ 0.0f, 0.0f, 0.0f, 1.0f });

    vk::RenderingAttachmentInfo attachmentInfo {
        .imageView   = m_State.View,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = loadOp,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = { .color = clearColor },
    };

    vk::RenderingInfo renderingInfo {
        .renderArea           = vk::Rect2D { .offset = {0, 0}, .extent = m_Extent },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachmentInfo,
    };

    cmd.beginRendering(renderingInfo);
}

void VulkanImage::State::Transition(vk::CommandBuffer cmd, vk::ImageLayout layout, vk::PipelineStageFlags2 stage, vk::AccessFlags2 access)
{
    // Nothing to wait for only when the layout stays and neither use writes (read after read).
    // Same layout after a write still needs the barrier: it's what makes the write visible.
    const vk::AccessFlags2 writes = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderStorageWrite
                                  | vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eTransferWrite
                                  | vk::AccessFlagBits2::eMemoryWrite;
    if (layout == Layout && !(Access & writes) && !(access & writes))
    {
        Stage  |= stage;
        Access |= access;
        return;
    }

    vk::ImageMemoryBarrier2 barrier {
        .srcStageMask        = Stage,
        .srcAccessMask       = Access,
        .dstStageMask        = stage,
        .dstAccessMask       = access,
        .oldLayout           = Layout,
        .newLayout           = layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = Image,
        .subresourceRange    = {
            .aspectMask      = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel    = 0,
            .levelCount      = 1,
            .baseArrayLayer  = 0,
            .layerCount      = 1
        }
    };

    Stage  = stage;
    Access = access;
    Layout = layout;

    cmd.pipelineBarrier2(vk::DependencyInfo {
        .imageMemoryBarrierCount  = 1,
        .pImageMemoryBarriers     = &barrier
    });
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER

