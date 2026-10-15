#pragma once

#include "vulkan/vulkan.hpp"

#include "../Renderer.hpp"
#include "../Application.hpp"

namespace Gram {
    class VulkanRenderer: public Renderer {
    public:
        VulkanRenderer(const RendererSpecification& rendererSpec);
        virtual ~VulkanRenderer() override;
        
        void PopulateDebugMessengerCreateInfo();
        void CreateDebugUtilsMessengerEXT();
        void DestroyDebugUtilsMessengerExt();

        bool PickPhysicalDevice();
        bool RatePhysicalDeviceSuitability(int32_t& suitabilityScore, const vk::PhysicalDevice& deviceInstance);

    private:
        vk::Instance m_VkInstance;
        vk::PhysicalDevice m_VkPhysicalDevice;
        vk::DebugUtilsMessengerCreateInfoEXT m_VkDebugMessengerCreateInfo;
        VkDebugUtilsMessengerEXT m_VkDebugMessenger = VK_NULL_HANDLE;
    };
}