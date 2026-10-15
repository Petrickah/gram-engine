#pragma once

#include <map>
#include <optional>

#include "vulkan/vulkan.hpp"

#include "../Renderer.hpp"
#include "../Application.hpp"

namespace Gram
{
    struct QueueFamilyIndices
    {
        float m_QueuePriority;
        vk::PhysicalDeviceFeatures m_DeviceFeatures;
        std::vector<vk::DeviceQueueCreateInfo> m_QueueCreateInfos;
        std::map<std::string, std::optional<uint32_t>> m_QueueFamiliesIndices;

        bool TryGetDeviceQueue(vk::Queue& deviceQueue, const vk::Device& device, const std::string& queueFamily);
        bool IsComplete();
    };

    class VulkanRenderer: public Renderer {
    public:
        VulkanRenderer(const RendererSpecification& rendererSpec);
        virtual ~VulkanRenderer() override;
        
        void PopulateDebugMessengerCreateInfo();
        void CreateDebugUtilsMessengerEXT();
        void DestroyDebugUtilsMessengerExt();

        bool PickPhysicalDevice();
        bool RatePhysicalDeviceSuitability(int32_t& suitabilityScore, const vk::PhysicalDevice& deviceInstance);
        bool FindQueueFamilies(const vk::PhysicalDevice& deviceInstance, QueueFamilyIndices& indices);
        bool CreateLogicalDevice();
        bool CreateSurfaceKHR();
        bool DestroySurfaceKHR();

    private:
        vk::Instance m_VkInstance;
        vk::Device m_VkDevice;
        vk::PhysicalDevice m_VkPhysicalDevice;
        vk::DebugUtilsMessengerCreateInfoEXT m_VkDebugMessengerCreateInfo;
        
        VkSurfaceKHR m_VkSurface = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_VkDebugMessenger = VK_NULL_HANDLE;

        QueueFamilyIndices m_VkQueueFamilyIndices;
    };
}