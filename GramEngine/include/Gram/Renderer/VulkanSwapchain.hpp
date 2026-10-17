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
    bool ChooseSwapchainExtent(vk::Extent2D& extent, const RendererSpecification *rendererSpec = nullptr, bool showDebug = false);
    bool ChooseSwapchainImageCount(uint32_t& imageCount, bool showDebug);
};

class VulkanSwapchain
{
public:
    VulkanSwapchain(const VulkanRendererSpecification* rendererSpec, QueueFamilyIndices& indices, vk::Device device, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, bool extensionsSupported = false);
    ~VulkanSwapchain();

public:
    const std::vector<vk::Image>& GetSwapchainImages() const { return m_SwapchainImages; }
    std::vector<vk::Image>& GetSwapchainImages() { return m_SwapchainImages; }

private:
    bool TryGetSwapchainImages(std::vector<vk::Image>& swapchainImages);
    bool QuerySwapchainSupport(const vk::PhysicalDevice& device, SwapChainSupportDetails& supportDetails, bool extensionsSupported);

private:
    vk::SwapchainKHR m_Swapchain;
    std::vector<vk::Image> m_SwapchainImages;

    VkDevice m_Device = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    SwapChainSupportDetails m_SupportDetails;
};

GRAM_NAMESPACE_END