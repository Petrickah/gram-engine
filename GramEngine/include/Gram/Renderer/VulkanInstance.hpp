#pragma once

#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanInstance : private UniqueObject
{
public:
    VulkanInstance(const VulkanRendererSpecification* rendererSpec);
    ~VulkanInstance();

public:
    VkInstance GetInstance() { return static_cast<VkInstance>(m_VkInstance); }

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