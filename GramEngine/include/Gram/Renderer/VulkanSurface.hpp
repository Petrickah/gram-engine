#pragma once

#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanSurface: private UniqueObject
{
public:
    VulkanSurface(const VulkanRendererSpecification* rendererSpec, const vk::Instance& instance);
    ~VulkanSurface();

public:
    VkSurfaceKHR GetSurface() { return m_VkSurface; }

private:
    bool CreateSurfaceKHR(const VulkanRendererSpecification* rendererSpec);
    bool DestroySurfaceKHR();

private:
    VkInstance m_VkInstance = VK_NULL_HANDLE;
    VkSurfaceKHR m_VkSurface = VK_NULL_HANDLE;
};

GRAM_NAMESPACE_END