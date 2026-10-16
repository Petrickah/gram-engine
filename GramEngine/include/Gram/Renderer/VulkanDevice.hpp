#pragma once

#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

struct QueueFamilyIndices
{
    std::map<std::string, std::optional<uint32_t>> m_QueueFamiliesIndices;

    bool TryGetDeviceQueue(vk::Queue& deviceQueue, const vk::Device& device, const std::string& queueFamily);
    bool IsComplete();
};

class VulkanDevice
{
public:
    VulkanDevice(const RendererSpecification* rendererSpec, const vk::Instance& instance, const VkSurfaceKHR& surface);
    ~VulkanDevice();

    bool PickPhysicalDevice(const RendererSpecification* rendererSpec);
    bool RatePhysicalDeviceSuitability(const RendererSpecification* rendererSpec, int32_t& suitabilityScore, const vk::PhysicalDevice& deviceInstance);
    bool FindQueueFamilies(const RendererSpecification* rendererSpec, const vk::PhysicalDevice& deviceInstance, QueueFamilyIndices& indices, bool showDebug = false);
    bool FindDeviceExtensions(const RendererSpecification* rendererSpec, const vk::PhysicalDevice& deviceInstance, std::vector<std::string>& extensions, bool showDebug = false);
    bool CreateLogicalDevice(const RendererSpecification* rendererSpec);

private:
    VkInstance m_VkInstance;
    VkSurfaceKHR m_VkSurface;

    std::vector<std::string> m_DeviceExtensions;
    std::vector<vk::DeviceQueueCreateInfo> m_QueueCreateInfos;
    
    vk::Device m_VkDevice;
    vk::PhysicalDevice m_VkPhysicalDevice;
    vk::PhysicalDeviceFeatures m_DeviceFeatures;
    QueueFamilyIndices m_QueueFamilyIndices;
};

GRAM_NAMESPACE_END