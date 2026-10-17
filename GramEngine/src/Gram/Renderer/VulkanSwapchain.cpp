#include "Gram/Renderer/Common.hpp"
#include "Gram/Renderer/VulkanSwapchain.hpp"

#include "Gram/Renderer/VulkanDevice.hpp"
#include "Gram/GLFWApplication.hpp"

GRAM_NAMESPACE_BEGIN

VulkanSwapchain::VulkanSwapchain(const VulkanRendererSpecification* rendererSpec, QueueFamilyIndices& indices, vk::Device device, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool extensionsSupported)
    : m_Device(device)
    , m_PhysicalDevice(physicalDevice)
    , m_Surface(surface)
{
    // Every step below reads the specification and talks to the device, so bail out early.
    if (!rendererSpec || !m_Device || !m_PhysicalDevice) return;

    if (rendererSpec->m_DisplayDebug)
    {
        std::cout << "[Vulkan] swapchain configuration: \n";
    }

    if (!QuerySwapchainSupport(m_PhysicalDevice, m_SupportDetails, extensionsSupported))
    {
        std::cerr << "   - [Vulkan] no swapchain support for the chosen device: " << vk::PhysicalDevice(m_PhysicalDevice).getProperties().deviceName << "\n";
        return;
    }

    vk::SurfaceFormatKHR surfaceFormat;
    if (!m_SupportDetails.ChooseSwapchainSurfaceFormat(surfaceFormat, rendererSpec->m_DisplayDebug))
    {
        std::cerr << "   - [Vulkan] no swapchain surface format to choose!\n";
        return;
    }

    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
    if (!m_SupportDetails.ChooseSwapchainPresentMode(presentMode, rendererSpec->m_DisplayDebug))
    {
        std::cerr << "   - [Vulkan] no swapchain surface present mode to choose!\n";
        return;
    }

    vk::Extent2D extent;
    if (!m_SupportDetails.ChooseSwapchainExtent(extent, rendererSpec, rendererSpec->m_DisplayDebug))
    {
        std::cerr << "   - [Vulkan] no swapchain surface extend mode to choose!\n";
        return;
    }

    uint32_t imageCount = 0;
    if (!m_SupportDetails.ChooseSwapchainImageCount(imageCount, rendererSpec->m_DisplayDebug))
    {
        std::cerr << "   - [Vulkan] no swapchain surface image count to choose!\n";
        return;
    }

    // Images are shared between queue families only when graphics and present are different
    // families; otherwise exclusive ownership is faster and needs no index list. A concurrent
    // swapchain requires unique indices, hence the set.
    std::vector<uint32_t> queueFamilyIndices;
    indices.TryGetDeviceIndices(queueFamilyIndices);

    const std::set<uint32_t> uniqueIndices(queueFamilyIndices.begin(), queueFamilyIndices.end());
    const bool concurrent = uniqueIndices.size() > 1;
    queueFamilyIndices.assign(uniqueIndices.begin(), uniqueIndices.end());

    vk::SwapchainCreateInfoKHR swapchainCreateInfo(
        vk::SwapchainCreateFlagsKHR(),                                           // Flags
        m_Surface,                                                               // Surface
        imageCount,                                                              // Image Count
        surfaceFormat.format,                                                    // Image Format
        surfaceFormat.colorSpace,                                                // Image Color Space
        extent,                                                                  // Image Extent
        1,                                                                       // Image Array Layers
        vk::ImageUsageFlagBits::eColorAttachment,                                // Image Usage
        concurrent ? vk::SharingMode::eConcurrent : vk::SharingMode::eExclusive, // Image Sharing Mode
        concurrent ? static_cast<uint32_t>(queueFamilyIndices.size()) : 0,       // Queue Family Index Count
        concurrent ? queueFamilyIndices.data() : nullptr,                        // Queue Family Indices
        m_SupportDetails.m_Capabilities.currentTransform,                        // Pre Transform Type
        vk::CompositeAlphaFlagBitsKHR::eOpaque,                                  // Composite Alpha Type
        presentMode,                                                             // Present Mode
        VK_TRUE,                                                                 // Is Clipped
        VK_NULL_HANDLE                                                           // Old Swapchain
    );

    vk::Result vkResult = device.createSwapchainKHR(&swapchainCreateInfo, nullptr, &m_Swapchain);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "   - [Vulkan] couldn't create the surface swapchain!" << vk::to_string(vkResult) << "\n";
        return;
    }

    if (!TryGetSwapchainImages(m_SwapchainImages))
    {
        std::cerr << "   - [Vulkan] couldn't retrieve the surface swapchain images!\n";
        return;
    }
}

VulkanSwapchain::~VulkanSwapchain()
{
    if (m_Swapchain) vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
}

bool VulkanSwapchain::QuerySwapchainSupport(const vk::PhysicalDevice &device, SwapChainSupportDetails &supportDetails, bool extensionsSupported)
{
    vk::Result vkResult = vk::Result::eSuccess;
    vkResult = device.getSurfaceCapabilitiesKHR(m_Surface, &supportDetails.m_Capabilities);

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "   - [Vulkan] swapchain surface capabilities are unknown: " << vk::to_string(vkResult) << "\n";
        return false;
    }

    uint32_t formatCount = 0;
    vkResult = device.getSurfaceFormatsKHR(m_Surface, &formatCount, nullptr);

    if (vkResult == vk::Result::eSuccess)
    {
        supportDetails.m_Formats.resize(formatCount);
        vkResult = device.getSurfaceFormatsKHR(m_Surface, &formatCount, supportDetails.m_Formats.data());
    }

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "   - [Vulkan] swapchain surface formats are unknown: " << vk::to_string(vkResult) << "\n";
        return false;
    }

    uint32_t presentModesCount = 0;
    vkResult = device.getSurfacePresentModesKHR(m_Surface, &presentModesCount, nullptr);

    if (vkResult == vk::Result::eSuccess)
    {
        supportDetails.m_PresentModes.resize(presentModesCount);
        vkResult = device.getSurfacePresentModesKHR(m_Surface, &presentModesCount, supportDetails.m_PresentModes.data());
    }

    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "   - [Vulkan] swapchain surface present modes are unknown: " << vk::to_string(vkResult) << "\n";
        return false;
    }

    return extensionsSupported && !supportDetails.m_Formats.empty() && !supportDetails.m_PresentModes.empty();
}

bool VulkanSwapchain::TryGetSwapchainImages(std::vector<vk::Image> &swapchainImages)
{
    uint32_t imageCount = 0;
    vk::Result vkResult = vk::Result::eSuccess;
    vk::Device device   = static_cast<vk::Device>(m_Device);

    vkResult = device.getSwapchainImagesKHR(m_Swapchain, &imageCount, nullptr);
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "   - [Vulkan] swapchain surface images not found: " << vk::to_string(vkResult) << "\n";
        return false;
    }

    swapchainImages.resize(imageCount);
    vkResult = device.getSwapchainImagesKHR(m_Swapchain, &imageCount, swapchainImages.data());
    if (vkResult != vk::Result::eSuccess)
    {
        std::cerr << "   - [Vulkan] swapchain surface images not found: " << vk::to_string(vkResult) << "\n";
        return false;
    }

    return true;
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
        std::cout << "   - [Vulkan] available surface formats: \n";
        for (const vk::SurfaceFormatKHR& available : m_Formats)
        {
            std::cout << (available == surfaceFormat
                          ? "        - [X] surface format: "
                          : "        - [-] surface format: ")
                      << vk::to_string(available.colorSpace) << "  "
                      << vk::to_string(available.format)     << "\n";
        }
    }

    return true;
}

bool SwapChainSupportDetails::ChooseSwapchainPresentMode(vk::PresentModeKHR &presentMode, bool showDebug)
{
    if (showDebug)
    {
        std::cout << "   - [Vulkan] available surface present modes: \n";
    }

    // Fifo is the only mode the spec guarantees, so it is also the fallback.
    presentMode = vk::PresentModeKHR::eFifo;

    bool hasFifo = false;
    for (const vk::PresentModeKHR& availableMode : m_PresentModes)
    {
        hasFifo = hasFifo || availableMode == vk::PresentModeKHR::eFifo;

        if (showDebug)
        {
            std::cout << (presentMode == availableMode
                          ? "        - [X] present mode: "
                          : "        - [-] present mode: ")
                      << vk::to_string(availableMode) << "\n";
        }
    }

    return hasFifo;
}

bool SwapChainSupportDetails::ChooseSwapchainExtent(vk::Extent2D &extent, const RendererSpecification *rendererSpec, bool showDebug)
{
    if (showDebug)
    {
        std::cout << "   - [Vulkan] available surface extents: \n";
    }

    // A defined currentExtent means the surface dictates the size; UINT32_MAX means we pick it ourselves.
    if (m_Capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        if (showDebug)
        {
            std::cout << "        - [X] surface extent: " << m_Capabilities.currentExtent.width << "x" << m_Capabilities.currentExtent.height << "px\n";
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
            std::cout << "        - [X] surface extent: " << extent.width << "x" << extent.height << "px\n";
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
        std::cout << "   - [Vulkan] available image counts: \n";
        std::cout << "        - [X] image count: " << imageCount << "\n";
    }

    return true;
}

GRAM_NAMESPACE_END
