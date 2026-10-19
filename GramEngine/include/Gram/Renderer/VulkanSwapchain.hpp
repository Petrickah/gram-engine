#pragma once

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
    bool ChooseSwapchainExtent(vk::Extent2D& extent, const RendererSpecification* rendererSpec = nullptr, bool showDebug = false);
    bool ChooseSwapchainImageCount(uint32_t& imageCount, bool showDebug);
};

class VulkanSwapchain : private UniqueObject
{
public:
    VulkanSwapchain(const VulkanRendererSpecification* rendererSpec, QueueFamilyIndices& indices, vk::Device device, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool extensionsSupported = false);
    ~VulkanSwapchain();
    
public:
    // m_ImageCount is only what we asked for; the driver may create more, so compare against the real images.
    bool IsValid() const { return m_Swapchain && !m_SwapchainImages.empty() && m_SwapchainImages.size() == m_SwapchainImageViews.size(); }

    const vk::Extent2D& GetExtent2D() const { return m_Extent2D; }
    const vk::SwapchainKHR& GetSwapchain() const { return m_Swapchain; }
    const vk::PresentModeKHR& GetPresentMode() const  { return m_PresentMode; }
    const vk::SurfaceFormatKHR& GetSurfaceFormat() const { return m_SurfaceFormat; }
    const std::vector<vk::ImageView>& GetSwapchainImageViews() const { return m_SwapchainImageViews; }
    
private:
    bool CreateSwapchain(std::vector<vk::Image>& swapchainImages, QueueFamilyIndices& indices, uint32_t requestedImageCount, const vk::SurfaceFormatKHR& surfaceFormat, const vk::Extent2D& extent, const vk::PresentModeKHR& presentMode);
    bool CreateSwapchainImageView(vk::ImageView& imageView, const vk::Image& image, const vk::SurfaceFormatKHR& surfaceFormat);
    bool QuerySwapchainSupport(const vk::PhysicalDevice& device, SwapChainSupportDetails& supportDetails, bool extensionsSupported);

private:
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