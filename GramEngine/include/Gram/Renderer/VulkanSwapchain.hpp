#pragma once
#ifdef GRAM_VULKAN_RENDERER
#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

struct QueueFamilyIndices;

struct SwapChainSupportDetails
{
    vk::SurfaceCapabilitiesKHR m_Capabilities;
    std::vector<vk::SurfaceFormatKHR> m_Formats;
    std::vector<vk::PresentModeKHR> m_PresentModes;

    bool ChooseSwapchainSurfaceFormat(vk::SurfaceFormatKHR& surfaceFormat, bool showDebug = false);
    bool ChooseSwapchainPresentMode(vk::PresentModeKHR& presentMode, bool showDebug = false);
    bool ChooseSwapchainExtent(vk::Extent2D& extent, const VulkanRendererSpecification* rendererSpec = nullptr, bool showDebug = false);
    bool ChooseSwapchainImageCount(uint32_t& imageCount, bool showDebug);
};

class VulkanSwapchain : private UniqueObject
{
public:
    VulkanSwapchain(const VulkanRendererSpecification* rendererSpec, QueueFamilyIndices& indices, vk::Device device, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool extensionsSupported = false);
    ~VulkanSwapchain();

public:
    // Rebuilds the swapchain for the surface's current size (window resized, or the old one
    // went out of date). Returns false while it can't be built, e.g. a minimized window.
    bool Recreate();

public:
    // m_ImageCount is only what we asked for; the driver may create more, so compare against the real images.
    bool IsValid() const { return m_Swapchain && !m_SwapchainImages.empty() && m_SwapchainImages.size() == m_SwapchainImageViews.size(); }

    uint32_t GetImageCount() const { return static_cast<uint32_t>(m_SwapchainImages.size()); }

    const vk::Extent2D& GetExtent2D() const { return m_Extent2D; }
    const vk::SwapchainKHR& GetSwapchain() const { return m_Swapchain; }
    const vk::PresentModeKHR& GetPresentMode() const  { return m_PresentMode; }
    const vk::SurfaceFormatKHR& GetSurfaceFormat() const { return m_SurfaceFormat; }
    const std::vector<vk::Image>& GetSwapchainImages() const { return m_SwapchainImages; }
    const std::vector<vk::ImageView>& GetSwapchainImageViews() const { return m_SwapchainImageViews; }
    
private:
    bool Build(bool showDebug);
    bool CreateSwapchain(std::vector<vk::Image>& swapchainImages, QueueFamilyIndices& indices, uint32_t requestedImageCount, const vk::SurfaceFormatKHR& surfaceFormat, const vk::Extent2D& extent, const vk::PresentModeKHR& presentMode);
    bool CreateSwapchainImageView(vk::ImageView& imageView, const vk::Image& image, const vk::SurfaceFormatKHR& surfaceFormat);
    bool QuerySwapchainSupport(const vk::PhysicalDevice& device, SwapChainSupportDetails& supportDetails, bool extensionsSupported);

private:
    const VulkanRendererSpecification* m_RendererSpec = nullptr;
    QueueFamilyIndices* m_Indices = nullptr;
    bool m_ExtensionsSupported = false;

    vk::Extent2D m_Extent2D;
    vk::SwapchainKHR m_Swapchain;
    vk::PresentModeKHR m_PresentMode = vk::PresentModeKHR::eFifo;
    vk::SurfaceFormatKHR m_SurfaceFormat;

    uint32_t m_ImageCount = 0;
    std::vector<vk::Image> m_SwapchainImages;
    std::vector<vk::ImageView> m_SwapchainImageViews;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    SwapChainSupportDetails m_SupportDetails;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER