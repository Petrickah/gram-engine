#pragma once
#ifdef GRAM_VULKAN_RENDERER
#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanInstance : private UniqueObject
{
public:
    VulkanInstance(const VulkanRendererSpecification* rendererSpec);
    ~VulkanInstance();

public:
    VkInstance GetInstance() const { return static_cast<VkInstance>(m_VkInstance); }

private:
    void PopulateDebugMessengerCreateInfo();
    void CreateDebugUtilsMessengerEXT();
    void DestroyDebugUtilsMessengerExt();

private:
    vk::Instance m_VkInstance;

    VkDebugUtilsMessengerEXT m_VkDebugMessenger = VK_NULL_HANDLE;
    vk::DebugUtilsMessengerCreateInfoEXT m_VkDebugMessengerCreateInfo;
};

GRAM_NAMESPACE_END
#endif // #ifdef GRAM_VULKAN_RENDERER