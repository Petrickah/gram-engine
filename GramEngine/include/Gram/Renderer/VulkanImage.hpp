#pragma once

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanDevice;

class VulkanImage : private MoveObject
{
public:
    // What an image knows about its last use, owning nothing: a VulkanImage holds one for the image
    // it owns, the swapchain one per image it owns. Transition() emits the barrier from that use to the next.
    struct State
    {
        vk::Image               Image  = {};
        vk::ImageView           View   = {};
        vk::Extent2D            Extent = {};
        vk::ImageLayout         Layout = vk::ImageLayout::eUndefined;
        vk::PipelineStageFlags2 Stage  = vk::PipelineStageFlagBits2::eNone;
        vk::AccessFlags2        Access = vk::AccessFlagBits2::eNone;

        void Transition(vk::CommandBuffer cmd, vk::ImageLayout layout, vk::PipelineStageFlags2 stage, vk::AccessFlags2 access);
    };

private:
    VulkanImage() = default;
    VulkanImage(vk::UniqueImage image, vk::UniqueDeviceMemory memory, vk::UniqueImageView imageView, vk::UniqueSampler sampler, vk::Extent2D extent, vk::Format format)
        : m_Image(std::move(image))
        , m_Memory(std::move(memory))
        , m_View(std::move(imageView))
        , m_Sampler(std::move(sampler))
        , m_Extent(extent)
        , m_Format(format)
    {
        // The raw handles of what this object owns; they stay the same when it's moved.
        m_State.Image  = *m_Image;
        m_State.View   = *m_View;
        m_State.Extent = m_Extent;
    }

public:
    static VulkanImage Create(VulkanDevice& device, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage);

    // A single image file loaded on a device-local image, ready to be read by the shader (eShaderReadOnlyOptimal).
    // eR8G8B8A8Srgb: image pixels are encoded as sRGB, the GPU reads them and converts in linear space.
    static VulkanImage Load(VulkanDevice& device, const std::filesystem::path& path, vk::Format format = vk::Format::eR8G8B8A8Srgb);

    // The Barrier from the last usage to the new one. The stage and olt usage are hold by the image,
    // so the pass tells only how the image gets used by it.
    void Transition(vk::CommandBuffer cmd, vk::ImageLayout layout, vk::PipelineStageFlags2 stage, vk::AccessFlags2 access)
    {
        m_State.Transition(cmd, layout, stage, access);
    }

    // Copies this whole image into the whole of `destination` (scaled when the sizes differ, converted when the
    // formats do), and leaves both in their transfer layouts. Both need the matching eTransferSrc/eTransferDst usage.
    void Blit(vk::CommandBuffer cmd, VulkanImage::State& destination, vk::Filter filter = vk::Filter::eLinear);

    void BeginRendering(vk::CommandBuffer cmd, vk::AttachmentLoadOp loadOp);
    bool IsValid() const { return m_Image && m_View && m_Sampler; }

    vk::Image          GetHandle()  const { return *m_Image;   }
    vk::ImageView      GetView()    const { return *m_View;    }
    vk::Sampler        GetSampler() const { return *m_Sampler; }
    vk::Extent2D       GetExtent()  const { return m_Extent;   }
    const VulkanImage::State& GetState() const { return m_State;    }
          VulkanImage::State& GetState()       { return m_State;    }

private:
    VulkanImage::State      m_State   = {};
    vk::UniqueImage         m_Image   = {};
    vk::UniqueDeviceMemory  m_Memory  = {};
    vk::UniqueImageView     m_View    = {};
    vk::UniqueSampler       m_Sampler = {};
    vk::Extent2D            m_Extent  = {};
    vk::Format              m_Format  = vk::Format::eR8G8B8A8Srgb;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
