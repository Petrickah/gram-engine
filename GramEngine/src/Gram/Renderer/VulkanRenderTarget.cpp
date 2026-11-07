#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanRenderTarget.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanRenderTarget::VulkanRenderTarget(VulkanDevice& device, vk::Extent2D extent, vk::Format format, vk::ImageUsageFlags usage)
    : m_Device(device)
    , m_Format(format)
    , m_Usage(usage)
    , m_Image(VulkanImage::Create(device, extent, format, usage))
{ }

void VulkanRenderTarget::RequestExtent(vk::Extent2D extent)
{
    if (extent.width == 0 || extent.height == 0) return;
    m_RequestedExtent = extent;
}

bool VulkanRenderTarget::ApplyRequestedExtent()
{
    if (!m_RequestedExtent) return false;

    const vk::Extent2D extent = *m_RequestedExtent;
    m_RequestedExtent.reset();

    const vk::Extent2D current = m_Image.GetExtent();
    if (extent.width == current.width && extent.height == current.height) return false;

    // The other frame in flight may still sample the old image (the viewport's ImGui texture).
    // A stall, only while resizing; deferring the destruction to that frame's fence would avoid it.
    GramVulkanCheck(static_cast<vk::Device>(m_Device.GetLogicalDevice()).waitIdle(), "VulkanRenderTarget: waiting for the device before resizing failed");

    // Moved out and destroyed first, so the old image is gone before the new one is allocated.
    { VulkanImage old = std::move(m_Image); }
    m_Image = VulkanImage::Create(m_Device, extent, m_Format, m_Usage);
    m_Generation++;

    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER
