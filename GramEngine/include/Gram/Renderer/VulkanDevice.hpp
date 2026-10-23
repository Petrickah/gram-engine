#pragma once
#ifdef GRAM_VULKAN_RENDERER
#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanFrame;

struct QueueFamilyIndices
{
    std::map<std::string, std::optional<uint32_t>> m_QueueFamiliesIndices;

    bool TryGetDeviceIndices(std::vector<uint32_t>& indices) const;
    bool TryGetDeviceQueue(vk::Queue& deviceQueue, const vk::Device& device, const std::string& queueFamily);
    bool IsComplete();
};

class VulkanDevice : private UniqueObject
{
public:
    VulkanDevice(const VulkanRendererSpecification* rendererSpec, const vk::Instance& instance, const vk::SurfaceKHR& surface);
    ~VulkanDevice();

public:
    // Returns the present result as-is: eSuboptimalKHR / eErrorOutOfDateKHR mean "recreate the swapchain", not "failed".
    vk::Result Submit(vk::Fence drawFence, uint32_t imageIndex, const std::vector<vk::Image>& images, const vk::SubmitInfo& submitInfo, const vk::PresentInfoKHR& presentInfo);
    bool AreExtensionsSupported(const VulkanRendererSpecification* rendererSpec)
    {
        // Only ask for extensions if the device actually offers all of them;
        return m_QueueFamilyIndices.IsComplete() && !rendererSpec->m_DeviceExtensions.empty() && rendererSpec->AreDeviceExtensionsSupported(m_DeviceExtensions);
    }

    QueueFamilyIndices& GetQueueFamilyIndices() { return m_QueueFamilyIndices; }
    const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

    VkDevice GetLogicalDevice() const  { return static_cast<VkDevice>(m_VkDevice); }
    VkPhysicalDevice GetPhysicalDevice() const { return static_cast<VkPhysicalDevice>(m_VkPhysicalDevice); }

private:
    bool PickPhysicalDevice(const VulkanRendererSpecification* rendererSpec);
    bool RatePhysicalDeviceSuitability(const VulkanRendererSpecification* rendererSpec, int32_t& suitabilityScore, const vk::PhysicalDevice& deviceInstance);
    bool FindQueueFamilies(const VulkanRendererSpecification* rendererSpec, const vk::PhysicalDevice& deviceInstance, QueueFamilyIndices& indices, bool showDebug = false);
    bool FindDeviceExtensions(const VulkanRendererSpecification* rendererSpec, const vk::PhysicalDevice& deviceInstance, std::vector<std::string>& extensions, bool showDebug = false);
    bool CreateLogicalDevice(const VulkanRendererSpecification* rendererSpec);

private:
    vk::Instance m_VkInstance;
    vk::SurfaceKHR m_VkSurface;

    std::vector<std::string> m_DeviceExtensions;
    std::vector<vk::DeviceQueueCreateInfo> m_QueueCreateInfos;
    
    vk::Device m_VkDevice;
    vk::PhysicalDevice m_VkPhysicalDevice;
    vk::PhysicalDeviceFeatures m_DeviceFeatures;
    QueueFamilyIndices m_QueueFamilyIndices;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER