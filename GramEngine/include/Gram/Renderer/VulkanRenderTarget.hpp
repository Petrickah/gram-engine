#pragma once

#include "Gram/Renderer/VulkanImage.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

class VulkanDevice;

// An image passes render into and something else shows (the editor's viewport panel), resized on
// request. Whoever shows it asks for a size (RequestExtent, while drawing the UI: too late for this
// frame, whose passes already rendered); the renderer applies it at the start of the next frame
// (ApplyRequestedExtent), before any pass records.
class VulkanRenderTarget : private UniqueObject
{
public:
    VulkanRenderTarget(VulkanDevice& device, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage);

    // Remembered until the next ApplyRequestedExtent; a zero size (a collapsed panel) is ignored. With a
    // fixed extent set, that one is used instead of `extent` (still times `scaleFactor`).
    void RequestExtent(vk::Extent2D extent, float scaleFactor = 1.0f);

    // A size that doesn't follow whoever shows the image (a resolution picked in a settings panel), or
    // nullopt to follow it again. The viewport and the present pass then fit the image into their area.
    void SetFixedExtent(std::optional<vk::Extent2D> extent) { m_FixedExtent = extent; }
    std::optional<vk::Extent2D> GetFixedExtent() const { return m_FixedExtent; }

    // Recreates the image when a different size was requested. Waits for the device to be idle
    // first: the frame still in flight may be reading the old image. Returns whether it recreated it.
    bool ApplyRequestedExtent();

    VulkanImage& GetImage()            { return m_Image;  }
    vk::Format   GetFormat()     const { return m_Format; }
    // Bumped on every recreation: what holds the image's view (an ImGui texture) rebuilds it on change.
    uint64_t     GetGeneration() const { return m_Generation; }

private:
    VulkanDevice&               m_Device;
    vk::Format                  m_Format;
    vk::ImageUsageFlags         m_Usage;
    VulkanImage                 m_Image;
    std::optional<vk::Extent2D> m_RequestedExtent;
    std::optional<vk::Extent2D> m_FixedExtent;
    uint64_t                    m_Generation = 0;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
