#pragma once

#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanInstance
{
public:
    VulkanInstance(const RendererSpecification* rendererSpec);
    ~VulkanInstance();

    vk::Instance& GetInstance() { return m_VkInstance; }
    const vk::Instance& GetInstance() const { return m_VkInstance; }

    void PopulateDebugMessengerCreateInfo();
    void CreateDebugUtilsMessengerEXT();
    void DestroyDebugUtilsMessengerExt();

private:
    vk::Instance m_VkInstance;

    VkDebugUtilsMessengerEXT m_VkDebugMessenger = VK_NULL_HANDLE;
    vk::DebugUtilsMessengerCreateInfoEXT m_VkDebugMessengerCreateInfo;
};

GRAM_NAMESPACE_END