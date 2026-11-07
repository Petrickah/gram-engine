#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

#include "Gram/Application/GLFWApplication.hpp"

#ifdef GRAM_VULKAN_RENDERER
GRAM_NAMESPACE_BEGIN

VulkanSwapchain::~VulkanSwapchain()
{
    vk::Device device = m_Device;
    for (VulkanImage::State& image : m_SwapchainImages) device.destroyImageView(image.View);
    
    if (m_Swapchain) device.destroySwapchainKHR(m_Swapchain);
}

VulkanSwapchain::VulkanSwapchain(const VulkanRendererSpecification* rendererSpec, QueueFamilyIndices& indices, vk::Device device, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool extensionsSupported)
    : m_RendererSpec(rendererSpec)
    , m_Indices(&indices)
    , m_ExtensionsSupported(extensionsSupported)
    , m_Device(device)
    , m_PhysicalDevice(physicalDevice)
    , m_Surface(surface)
{
    // Every step below reads the specification and talks to the device, so bail out early.
    if (!rendererSpec || !m_Device || !m_PhysicalDevice) return;

    Build(rendererSpec->m_DisplayDebug);
}

bool VulkanSwapchain::Recreate()
{
    if (!m_RendererSpec || !m_Device || !m_PhysicalDevice) return false;

    // The old images and views may still be in flight.
    vk::Device device = m_Device;
    GramVulkanCheck(device.waitIdle(), "waiting for the device before recreating the swapchain failed");

    return Build(false);
}

bool VulkanSwapchain::Build(bool showDebug)
{
    if (showDebug)
    {
        GramVulkanInfo("swapchain configuration:");
    }

    // Capabilities (notably currentExtent) change with the window, so they are queried on every build.
    if (!QuerySwapchainSupport(m_PhysicalDevice, m_SupportDetails, m_ExtensionsSupported))
    {
        GramVulkanError("no swapchain support for the chosen device: {}", vk::PhysicalDevice(m_PhysicalDevice).getProperties().deviceName.data());
        return false;
    }

    vk::Extent2D extent;
    if (!m_SupportDetails.ChooseSwapchainExtent(extent, m_RendererSpec, showDebug))
    {
        GramVulkanError("no swapchain surface extent to choose");
        return false;
    }

    // A minimized window reports a 0x0 surface, which can't back a swapchain. Not an error:
    // the caller just retries once the window has a size again.
    if (extent.width == 0 || extent.height == 0) return false;

    if (!m_SupportDetails.ChooseSwapchainSurfaceFormat(m_SurfaceFormat, showDebug))
    {
        GramVulkanError("no swapchain surface format to choose");
        return false;
    }

    if (!m_SupportDetails.ChooseSwapchainPresentMode(m_PresentMode, showDebug))
    {
        GramVulkanError("no swapchain surface present mode to choose");
        return false;
    }

    if (!m_SupportDetails.ChooseSwapchainImageCount(m_ImageCount, showDebug))
    {
        GramVulkanError("no swapchain surface image count to choose");
        return false;
    }

    vk::Device device = m_Device;
    for (VulkanImage::State image : m_SwapchainImages) device.destroyImageView(image.View);
    m_SwapchainImages.clear();

    m_Extent2D = extent;
    if (!CreateSwapchain(m_SwapchainImages, *m_Indices, m_ImageCount, m_SurfaceFormat, m_Extent2D, m_PresentMode))
    {
        return false;
    }

    return true;
}

bool VulkanSwapchain::CreateSwapchainImageView(vk::ImageView& imageView, const vk::Image& image, const vk::SurfaceFormatKHR& surfaceFormat)
{
    vk::ImageViewCreateInfo vkImageViewCreateInfo {
        .image            = image,
        .viewType         = vk::ImageViewType::e2D,
        .format           = surfaceFormat.format,
        .components       = {
            .r = vk::ComponentSwizzle::eIdentity,
            .g = vk::ComponentSwizzle::eIdentity,
            .b = vk::ComponentSwizzle::eIdentity,
            .a = vk::ComponentSwizzle::eIdentity
        },
        .subresourceRange = {
            .aspectMask     = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    vk::Device vkDevice = m_Device;
    GramVulkanCheck(vkDevice.createImageView(&vkImageViewCreateInfo, nullptr, &imageView), "couldn't create the surface image views");

    return true;
}

bool VulkanSwapchain::CreateSwapchain(std::vector<VulkanImage::State>& swapchainImages, QueueFamilyIndices& indices, uint32_t requestedImageCount, const vk::SurfaceFormatKHR& surfaceFormat, const vk::Extent2D& extent, const vk::PresentModeKHR& presentMode)
{
    vk::Device vkDevice = m_Device;

    // Images are shared between queue families only when graphics and present are different
    // families; otherwise exclusive ownership is faster and needs no index list. A concurrent
    // swapchain requires unique indices.
    const std::vector<uint32_t> uniqueIndices = indices.GetUniqueFamilies();
    GramCheck(!uniqueIndices.empty(), "error reading the family indexes: queue families are incomplete");

    const bool concurrent = uniqueIndices.size() > 1;

    vk::SwapchainCreateInfoKHR swapchainCreateInfo {
        .surface               = m_Surface,
        .minImageCount         = requestedImageCount,
        .imageFormat           = surfaceFormat.format,
        .imageColorSpace       = surfaceFormat.colorSpace,
        .imageExtent           = extent,
        .imageArrayLayers      = 1,
        .imageUsage            = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode      = concurrent ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive,
        .queueFamilyIndexCount = concurrent ? static_cast<uint32_t>(uniqueIndices.size()) : 0,
        .pQueueFamilyIndices   = concurrent ? uniqueIndices.data() : nullptr,
        .preTransform          = m_SupportDetails.m_Capabilities.currentTransform,
        .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode           = presentMode,
        .clipped               = vk::True,
        .oldSwapchain          = m_Swapchain
    };

    vk::SwapchainKHR newSwapchain;
    GramVulkanCheck(vkDevice.createSwapchainKHR(&swapchainCreateInfo, nullptr, &newSwapchain), "couldn't create the surface swapchain");

    // The old swapchain is retired by the create call above; its resources can go now.
    if (m_Swapchain) vkDevice.destroySwapchainKHR(m_Swapchain);
    m_Swapchain = newSwapchain;

    uint32_t imageCount = 0;
    GramVulkanCheck(vkDevice.getSwapchainImagesKHR(m_Swapchain, &imageCount, nullptr), "swapchain surface images not found");
    
    std::vector<vk::Image> images(imageCount); // swapchainImages.resize(imageCount);
    GramVulkanCheck(vkDevice.getSwapchainImagesKHR(m_Swapchain, &imageCount, images.data()), "swapchain surface images not found");

    for (const vk::Image& image: images)
    {
        vk::ImageView imageView;
        if (!CreateSwapchainImageView(imageView, image, m_SurfaceFormat))
        {
            return false; // IsValid() reports the mismatch; the views created so far are freed by the destructor.
        }

        swapchainImages.push_back(VulkanImage::State { 
            .Image  = image, 
            .View   = imageView
        });
    }

    return true;
}

bool VulkanSwapchain::QuerySwapchainSupport(const vk::PhysicalDevice &device, SwapChainSupportDetails &supportDetails, bool extensionsSupported)
{
    GramVulkanCheck(device.getSurfaceCapabilitiesKHR(m_Surface, &supportDetails.m_Capabilities), "swapchain surface capabilities are unknown");

    uint32_t formatCount = 0;
    GramVulkanCheck(device.getSurfaceFormatsKHR(m_Surface, &formatCount, nullptr), "swapchain surface formats are unknown");

    supportDetails.m_Formats.resize(formatCount);
    GramVulkanCheck(device.getSurfaceFormatsKHR(m_Surface, &formatCount, supportDetails.m_Formats.data()), "swapchain surface formats are unknown");

    uint32_t presentModesCount = 0;
    GramVulkanCheck(device.getSurfacePresentModesKHR(m_Surface, &presentModesCount, nullptr), "swapchain surface present modes are unknown");

    supportDetails.m_PresentModes.resize(presentModesCount);
    GramVulkanCheck(device.getSurfacePresentModesKHR(m_Surface, &presentModesCount, supportDetails.m_PresentModes.data()), "swapchain surface present modes are unknown");

    return extensionsSupported && !supportDetails.m_Formats.empty() && !supportDetails.m_PresentModes.empty();
}

bool SwapChainSupportDetails::ChooseSwapchainSurfaceFormat(vk::SurfaceFormatKHR &surfaceFormat, bool showDebug)
{
    if (m_Formats.empty())
    {
        return false;
    }

    // Preferred: 8-bit BGRA sRGB. Then any sRGB-nonlinear format. Then whatever the surface offers first.
    auto chosen = std::find_if(m_Formats.begin(), m_Formats.end(), [](const vk::SurfaceFormatKHR& available) {
        return available.format == vk::Format::eB8G8R8A8Srgb && available.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });

    if (chosen == m_Formats.end())
    {
        chosen = std::find_if(m_Formats.begin(), m_Formats.end(), [](const vk::SurfaceFormatKHR& available) {
            return available.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    }

    if (chosen == m_Formats.end())
    {
        chosen = m_Formats.begin();
    }

    surfaceFormat = *chosen;

    if (showDebug)
    {
        for (const vk::SurfaceFormatKHR& available : m_Formats)
        {
            GramVulkanListItem(available == surfaceFormat, "surface format: {}  {}", vk::to_string(available.colorSpace), vk::to_string(available.format));
        }
    }

    return true;
}

bool SwapChainSupportDetails::ChooseSwapchainPresentMode(vk::PresentModeKHR &presentMode, bool showDebug)
{
    // Fifo is the only mode the spec guarantees, so it is also the fallback.
    presentMode = vk::PresentModeKHR::eFifo;

    bool hasFifo = false;
    for (const vk::PresentModeKHR& availableMode : m_PresentModes)
    {
        hasFifo = hasFifo || availableMode == vk::PresentModeKHR::eFifo;

        if (showDebug)
        {
            GramVulkanListItem(presentMode == availableMode, "present mode: {}", vk::to_string(availableMode));
        }
    }

    return hasFifo;
}

bool SwapChainSupportDetails::ChooseSwapchainExtent(vk::Extent2D &extent, const VulkanRendererSpecification *rendererSpec, bool showDebug)
{
    // A defined currentExtent means the surface dictates the size; UINT32_MAX means we pick it ourselves.
    if (m_Capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        if (showDebug)
        {
            GramVulkanListSelected("surface extent: {}x{}px", m_Capabilities.currentExtent.width, m_Capabilities.currentExtent.height);
        }

        extent = m_Capabilities.currentExtent;
        return true;
    }

    if (!rendererSpec) return false;

    if (GLFWApplication* glfwApplication = dynamic_cast<GLFWApplication*>(rendererSpec->m_Application))
    {
        int width, height;
        glfwGetFramebufferSize(glfwApplication->GetWindow(), &width, &height);

        extent = vk::Extent2D(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        extent.width  = std::clamp(extent.width, m_Capabilities.minImageExtent.width, m_Capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, m_Capabilities.minImageExtent.height, m_Capabilities.maxImageExtent.height);

        if (showDebug)
        {
            GramVulkanListSelected("surface extent: {}x{}px", extent.width, extent.height);
        }

        return true;
    }

    return false;
}

bool SwapChainSupportDetails::ChooseSwapchainImageCount(uint32_t& imageCount, bool showDebug)
{
    imageCount = m_Capabilities.minImageCount + 1;
    if (m_Capabilities.maxImageCount > 0 && m_Capabilities.maxImageCount < imageCount)
    {
        imageCount = m_Capabilities.maxImageCount;
    }
    
    if (showDebug)
    {
        GramVulkanListSelected("image count: {}", imageCount);
    }

    return true;
}

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER