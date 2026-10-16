#pragma once

#include "Renderer.hpp"

GRAM_NAMESPACE_BEGIN

class VulkanSurface
{
public:
    VulkanSurface(const RendererSpecification* rendererSpec, const vk::Instance& instance);
    ~VulkanSurface();

    bool CreateSurfaceKHR(const RendererSpecification* rendererSpec);
    bool DestroySurfaceKHR();

    VkSurfaceKHR& GetSurface() { return m_VkSurface; }
    const VkSurfaceKHR& GetSurface() const { return m_VkSurface; }

private:
    VkInstance m_VkInstance;
    VkSurfaceKHR m_VkSurface = VK_NULL_HANDLE;
};

GRAM_NAMESPACE_END