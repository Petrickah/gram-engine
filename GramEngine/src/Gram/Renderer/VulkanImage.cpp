#include "Gram/Renderer/VulkanBuffer.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanImage.hpp"

// The one translation unit that compiles stb_image: its implementation has no include guard,
// so a second STB_IMAGE_IMPLEMENTATION (or this header included again under it) redefines everything.
#define STB_IMAGE_IMPLEMENTATION
#ifdef __MINGW32__
// mingw-w64's GCC 13 rejects stb_image's SSE2 resampler ("the last argument must be an 8-bit
// immediate" from _mm_slli_si128 once inlined). stb only turns SIMD off for 32-bit mingw, so do it
// here; the scalar path is plenty for loading a texture, and the other platforms keep their SIMD.
#define STBI_NO_SIMD
#endif
#include <stb_image.h>

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanImage VulkanImage::Load(VulkanDevice& device, const std::filesystem::path& path, vk::Format format)
{
    GramCheck_(std::filesystem::exists(path), return VulkanImage(), "VulkanImage::Load: image not found {}", path.string());

    int width = 0, height = 0, channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha), &stbi_image_free);
    GramCheck_(pixels != nullptr, return VulkanImage {}, "VulkanImage::Load: can't load the image {}: {}", path.string(), stbi_failure_reason());
    const vk::DeviceSize size = static_cast<vk::DeviceSize>(width) * height * 4;   // STBI_rgb_alpha: always 4 bytes per pixel

    VulkanBuffer staging = VulkanBuffer::Create(device, size, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, vk::BufferUsageFlagBits::eTransferSrc);
    staging.Map<stbi_uc, VulkanBuffer::Access::Write>().Write(pixels.get(), 0, size);

    const vk::Extent2D extent { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    VulkanImage image = VulkanImage::Create(device, extent, format, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);

    vk::Result result = device.Submit([&](vk::CommandBuffer cmd)
    {
        image.Transition(cmd, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite);
        cmd.copyBufferToImage(staging.Get(), image.m_State.Image, vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy {
            .bufferOffset      = 0,
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = { .aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
            .imageOffset       = { 0, 0, 0 },
            .imageExtent       = { extent.width, extent.height, 1 }
        });
        image.Transition(cmd, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        return vk::Result::eSuccess;
    });

    GramVulkanCheck_(result, "VulkanImage::Load: error uploading the image", return VulkanImage {});
    return image;
}

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
        .magFilter     = vk::Filter::eLinear,
        .minFilter     = vk::Filter::eLinear,
        .mipmapMode    = vk::SamplerMipmapMode::eLinear,
        .addressModeU  = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV  = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW  = vk::SamplerAddressMode::eClampToEdge,
        .maxLod        = 0.0f,
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

